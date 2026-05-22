#include "codec/dic_roi_codec.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec/dic_predict.h"
#include "codec/dic_quant.h"
#include "wavelet/dic_dwt53.h"

/* Principle: JPEG 2000 codestream/file data is byte-oriented; see ITU-T T.800 marker syntax overview:
 * https://www.itu.int/dms_pubrec/itu-t/rec/t/T-REC-T.800-201906-S!!TOC-HTM-E.htm */
static FILE *dic_roi_open_file(const char *path, const char *mode)
{
    FILE *file = NULL;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, mode) != 0)
        return NULL;
    return file;
#else
    return fopen(path, mode);
#endif
}

/* Principle: JPEG 2000 marker segments use big/little-endian-defined binary fields; this project uses explicit LE fields for custom ROI streams:
 * https://en.wikipedia.org/wiki/Endianness */
static int dic_roi_write_u32_le(FILE *file, uint32_t value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24) & 0xffu);
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes);
}

/* Principle: Explicit byte-order decoding avoids host-endian ambiguity:
 * https://en.wikipedia.org/wiki/Endianness */
static int dic_roi_read_u32_le(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];

    if (file == NULL || value == NULL)
        return 0;
    if (fread(bytes, 1u, sizeof(bytes), file) != sizeof(bytes))
        return 0;

    *value = (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
    return 1;
}

/* Principle: Luma-weighted RGB conversion follows the common Rec. 601-style idea of extracting intensity before edge analysis:
 * https://en.wikipedia.org/wiki/Luma_(video) */
static int dic_roi_luminance_at(
    const uint8_t *input,
    int width,
    int channels,
    int x,
    int y
)
{
    size_t pixel = ((size_t)y * (size_t)width) + (size_t)x;

    if (channels == 1)
        return (int)input[pixel];

    return ((int)input[pixel * 3u] * 77
        + (int)input[(pixel * 3u) + 1u] * 150
        + (int)input[(pixel * 3u) + 2u] * 29) >> 8;
}

/* Principle: Clamping constrains computed ROI bounds to the valid image domain:
 * https://en.wikipedia.org/wiki/Clamping_(graphics) */
static int dic_roi_clamp_int(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

/* Principle: Resource ownership follows init/free pairing for deterministic C lifetime management:
 * https://en.cppreference.com/w/c/memory */
void dic_roi_encoded_init(dic_roi_encoded_image *encoded)
{
    if (encoded == NULL)
        return;

    encoded->width = 0;
    encoded->height = 0;
    encoded->channels = 0;
    encoded->levels = 0;
    encoded->quant_step = 0;
    encoded->roi_shift = 0;
    encoded->roi_rect.x = 0;
    encoded->roi_rect.y = 0;
    encoded->roi_rect.width = 0;
    encoded->roi_rect.height = 0;
    encoded->channel_streams = NULL;
}

/* Principle: Resource ownership follows init/free pairing for deterministic C lifetime management:
 * https://en.cppreference.com/w/c/memory */
void dic_roi_encoded_free(dic_roi_encoded_image *encoded)
{
    int channel;

    if (encoded == NULL)
        return;

    if (encoded->channel_streams != NULL)
    {
        for (channel = 0; channel < encoded->channels; ++channel)
            dic_bitplane_stream_free(&encoded->channel_streams[channel].bitplanes);
    }

    free(encoded->channel_streams);
    dic_roi_encoded_init(encoded);
}

/* Principle: Automatic ROI detection uses image-gradient energy, following the edge-emphasis idea behind Sobel-style operators:
 * https://en.wikipedia.org/wiki/Sobel_operator */
dic_status dic_roi_detect_auto(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    dic_rect_i32 *roi_rect
)
{
    double sum = 0.0;
    double sum_sq = 0.0;
    int count = 0;
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    int x;
    int y;
    double mean;
    double variance;
    double threshold;

    if (input == NULL || roi_rect == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (width <= 0 || height <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;
    if (channels != 1 && channels != 3)
        return DIC_HW4_INVALID_CHANNELS;

    if (width < 3 || height < 3)
    {
        roi_rect->x = 0;
        roi_rect->y = 0;
        roi_rect->width = width;
        roi_rect->height = height;
        return DIC_STATUS_OK;
    }

    for (y = 1; y < height - 1; ++y)
    {
        for (x = 1; x < width - 1; ++x)
        {
            int gx = dic_roi_luminance_at(input, width, channels, x + 1, y)
                - dic_roi_luminance_at(input, width, channels, x - 1, y);
            int gy = dic_roi_luminance_at(input, width, channels, x, y + 1)
                - dic_roi_luminance_at(input, width, channels, x, y - 1);
            double energy = (double)(gx * gx + gy * gy);

            sum += energy;
            sum_sq += energy * energy;
            ++count;
        }
    }

    mean = count > 0 ? sum / (double)count : 0.0;
    variance = count > 0 ? (sum_sq / (double)count) - (mean * mean) : 0.0;
    if (variance < 0.0)
        variance = 0.0;
    threshold = mean + variance * 0.0005;
    if (threshold < mean * 2.0)
        threshold = mean * 2.0;

    min_x = width;
    min_y = height;
    max_x = -1;
    max_y = -1;
    for (y = 1; y < height - 1; ++y)
    {
        for (x = 1; x < width - 1; ++x)
        {
            int gx = dic_roi_luminance_at(input, width, channels, x + 1, y)
                - dic_roi_luminance_at(input, width, channels, x - 1, y);
            int gy = dic_roi_luminance_at(input, width, channels, x, y + 1)
                - dic_roi_luminance_at(input, width, channels, x, y - 1);
            double energy = (double)(gx * gx + gy * gy);

            if (energy < threshold)
                continue;

            if (x < min_x)
                min_x = x;
            if (x > max_x)
                max_x = x;
            if (y < min_y)
                min_y = y;
            if (y > max_y)
                max_y = y;
        }
    }

    if (max_x < min_x || max_y < min_y)
    {
        int fallback_width = width / 2;
        int fallback_height = height / 2;

        roi_rect->x = (width - fallback_width) / 2;
        roi_rect->y = (height - fallback_height) / 2;
        roi_rect->width = fallback_width > 0 ? fallback_width : width;
        roi_rect->height = fallback_height > 0 ? fallback_height : height;
        return DIC_STATUS_OK;
    }

    min_x = dic_roi_clamp_int(min_x - (width / 32) - 2, 0, width - 1);
    min_y = dic_roi_clamp_int(min_y - (height / 32) - 2, 0, height - 1);
    max_x = dic_roi_clamp_int(max_x + (width / 32) + 2, 0, width - 1);
    max_y = dic_roi_clamp_int(max_y + (height / 32) + 2, 0, height - 1);

    roi_rect->x = min_x;
    roi_rect->y = min_y;
    roi_rect->width = max_x - min_x + 1;
    roi_rect->height = max_y - min_y + 1;
    return DIC_STATUS_OK;
}

/* Principle: JPEG 2000 Part 1 validates image/component geometry before wavelet coding:
 * https://www.itu.int/dms_pubrec/itu-t/rec/t/T-REC-T.800-201906-S!!TOC-HTM-E.htm */
static dic_status dic_roi_validate_params(
    int width,
    int height,
    int channels,
    int levels,
    int quant_step
)
{
    dic_status status;

    if (channels != 1 && channels != 3)
        return DIC_HW4_INVALID_CHANNELS;
    if (quant_step <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK)
        return status;

    return DIC_STATUS_OK;
}

/* Principle: JPEG 2000 processes image components independently before optional multi-component transforms:
 * https://jpeg.org/jpeg2000/ */
static void dic_roi_copy_channel_to_plane(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int channel,
    int32_t *plane
)
{
    int y;

    for (y = 0; y < height; ++y)
    {
        int x;

        for (x = 0; x < width; ++x)
        {
            size_t pixel = ((size_t)y * (size_t)width) + (size_t)x;
            plane[pixel] = (int32_t)input[(pixel * (size_t)channels) + (size_t)channel];
        }
    }
}

/* Principle: Reconstructed integer samples are clipped to the valid unsigned 8-bit display range:
 * https://en.wikipedia.org/wiki/Clamping_(graphics) */
static uint8_t dic_roi_clamp_u8(int32_t value)
{
    if (value < 0)
        return 0u;
    if (value > 255)
        return 255u;
    return (uint8_t)value;
}

/* Principle: Component planes are interleaved back into an image buffer for PGM/PPM output:
 * https://netpbm.sourceforge.net/doc/ppm.html */
static void dic_roi_copy_plane_to_channel(
    const int32_t *plane,
    int width,
    int height,
    int channels,
    int channel,
    uint8_t *output
)
{
    int y;

    for (y = 0; y < height; ++y)
    {
        int x;

        for (x = 0; x < width; ++x)
        {
            size_t pixel = ((size_t)y * (size_t)width) + (size_t)x;
            output[(pixel * (size_t)channels) + (size_t)channel] =
                dic_roi_clamp_u8(plane[pixel]);
        }
    }
}

/* Principle: JPEG 2000 ROI Maxshift prioritizes ROI coefficients by moving their bits into higher bit-planes:
 * https://www.sciencedirect.com/science/article/pii/S0923596501000261 */
static int32_t dic_roi_shift_value(int32_t value, int shift)
{
    uint32_t magnitude;

    if (value == 0 || shift <= 0)
        return value;

    magnitude = value < 0
        ? (uint32_t)(-(value + 1)) + 1u
        : (uint32_t)value;
    magnitude <<= (unsigned int)shift;
    return value < 0 ? -((int32_t)magnitude) : (int32_t)magnitude;
}

/* Principle: JPEG 2000 ROI Maxshift decoding reverses the coefficient scaling after bit-plane decoding:
 * https://www.sciencedirect.com/science/article/pii/S0923596501000261 */
static int32_t dic_roi_unshift_value(int32_t value, int shift)
{
    uint32_t magnitude;

    if (value == 0 || shift <= 0)
        return value;

    magnitude = value < 0
        ? (uint32_t)(-(value + 1)) + 1u
        : (uint32_t)value;
    magnitude >>= (unsigned int)shift;
    return value < 0 ? -((int32_t)magnitude) : (int32_t)magnitude;
}

/* Principle: A rectangular ROI mask is a simple coefficient mask for Maxshift-style prioritization:
 * https://www.sciencedirect.com/science/article/pii/S0923596501000261 */
static void dic_roi_apply_shift_to_rect(
    int32_t *plane,
    int width,
    int height,
    dic_rect_i32 rect,
    int shift,
    int inverse
)
{
    int y;

    for (y = rect.y; y < rect.y + rect.height && y < height; ++y)
    {
        int x;

        if (y < 0)
            continue;

        for (x = rect.x; x < rect.x + rect.width && x < width; ++x)
        {
            int32_t *value;

            if (x < 0)
                continue;

            value = plane + ((size_t)y * (size_t)width) + (size_t)x;
            *value = inverse
                ? dic_roi_unshift_value(*value, shift)
                : dic_roi_shift_value(*value, shift);
        }
    }
}

/* Principle: JPEG 2000 ROI coding applies wavelet transform, quantization, ROI coefficient scaling, then embedded bit-plane coding:
 * https://www.sciencedirect.com/science/article/pii/S0923596501000261 */
dic_status dic_roi_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int levels,
    int quant_step,
    dic_roi_encoded_image *encoded
)
{
    size_t plane_count;
    int32_t *plane = NULL;
    dic_rect_i32 ll_rect;
    dic_rect_i32 roi_rect;
    dic_status status;
    int channel;

    if (input == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_roi_validate_params(width, height, channels, levels, quant_step);
    if (status != DIC_STATUS_OK)
        return status;

    status = dic_roi_detect_auto(input, width, height, channels, &roi_rect);
    if (status != DIC_STATUS_OK)
        return status;

    plane_count = (size_t)width * (size_t)height;
    plane = (int32_t *)malloc(plane_count * sizeof(plane[0]));
    if (plane == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    dic_roi_encoded_free(encoded);
    encoded->channel_streams = (dic_roi_channel_stream *)calloc(
        (size_t)channels,
        sizeof(encoded->channel_streams[0])
    );
    if (encoded->channel_streams == NULL)
    {
        free(plane);
        return DIC_STATUS_MEMORY_ERROR;
    }

    encoded->width = width;
    encoded->height = height;
    encoded->channels = channels;
    encoded->levels = levels;
    encoded->quant_step = quant_step;
    encoded->roi_shift = DIC_ROI_DEFAULT_SHIFT;
    encoded->roi_rect = roi_rect;

    status = dic_subband_lowest_ll_rect(width, height, levels, &ll_rect);
    if (status != DIC_STATUS_OK)
    {
        free(plane);
        dic_roi_encoded_free(encoded);
        return status;
    }

    for (channel = 0; channel < channels; ++channel)
    {
        dic_roi_copy_channel_to_plane(input, width, height, channels, channel, plane);

        status = dic_dwt53_forward_plane(plane, width, height, levels);
        if (status == DIC_STATUS_OK)
            status = dic_quant_scalar_i32(plane, plane_count, quant_step);
        if (status == DIC_STATUS_OK)
            status = dic_predict_ll_left(plane, width, ll_rect);
        if (status == DIC_STATUS_OK)
            dic_roi_apply_shift_to_rect(plane, width, height, roi_rect, encoded->roi_shift, 0);
        if (status == DIC_STATUS_OK)
        {
            status = dic_bitplane_encode_i32(
                plane,
                plane_count,
                &encoded->channel_streams[channel].bitplanes
            );
        }

        if (status != DIC_STATUS_OK)
        {
            free(plane);
            dic_roi_encoded_free(encoded);
            return status;
        }
    }

    free(plane);
    return DIC_STATUS_OK;
}

/* Principle: Embedded bit-plane codestreams can be truncated; ROI Maxshift makes ROI bits appear earlier in that ordering:
 * https://www.sciencedirect.com/science/article/pii/S0923596501000261 */
dic_status dic_roi_decode_image(
    const dic_roi_encoded_image *encoded,
    int decoded_bitplanes,
    dic_image_u8 *decoded
)
{
    size_t plane_count;
    int32_t *plane = NULL;
    dic_rect_i32 ll_rect;
    dic_status status;
    int channel;

    if (encoded == NULL || decoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_roi_validate_params(
        encoded->width,
        encoded->height,
        encoded->channels,
        encoded->levels,
        encoded->quant_step
    );
    if (status != DIC_STATUS_OK)
        return status;

    plane_count = (size_t)encoded->width * (size_t)encoded->height;
    plane = (int32_t *)malloc(plane_count * sizeof(plane[0]));
    if (plane == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    status = dic_image_u8_alloc(decoded, encoded->width, encoded->height, encoded->channels);
    if (status != DIC_STATUS_OK)
    {
        free(plane);
        return status;
    }

    status = dic_subband_lowest_ll_rect(encoded->width, encoded->height, encoded->levels, &ll_rect);
    if (status != DIC_STATUS_OK)
    {
        dic_image_u8_free(decoded);
        free(plane);
        return status;
    }

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        status = dic_bitplane_decode_i32(
            &encoded->channel_streams[channel].bitplanes,
            decoded_bitplanes,
            plane
        );
        if (status == DIC_STATUS_OK)
            dic_roi_apply_shift_to_rect(plane, encoded->width, encoded->height, encoded->roi_rect, encoded->roi_shift, 1);
        if (status == DIC_STATUS_OK)
            status = dic_unpredict_ll_left(plane, encoded->width, ll_rect);
        if (status == DIC_STATUS_OK)
            status = dic_dequant_scalar_i32(plane, plane_count, encoded->quant_step);
        if (status == DIC_STATUS_OK)
            status = dic_dwt53_inverse_plane(plane, encoded->width, encoded->height, encoded->levels);

        if (status != DIC_STATUS_OK)
        {
            dic_image_u8_free(decoded);
            free(plane);
            return status;
        }

        dic_roi_copy_plane_to_channel(
            plane,
            encoded->width,
            encoded->height,
            encoded->channels,
            channel,
            decoded->data
        );
    }

    free(plane);
    return DIC_STATUS_OK;
}

/* Principle: This custom ROI file persists the metadata needed to invert Maxshift and decode the embedded bit-plane stream:
 * https://www.sciencedirect.com/science/article/pii/S0923596501000261 */
dic_status dic_roi_write_file(
    const char *path,
    const dic_roi_encoded_image *encoded
)
{
    FILE *file = NULL;
    dic_status status = DIC_STATUS_OK;
    int channel;

    if (path == NULL || encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = dic_roi_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (fwrite(DIC_ROI_FILE_MAGIC, 1u, 4u, file) != 4u
        || !dic_roi_write_u32_le(file, DIC_ROI_FILE_VERSION)
        || !dic_roi_write_u32_le(file, (uint32_t)encoded->width)
        || !dic_roi_write_u32_le(file, (uint32_t)encoded->height)
        || !dic_roi_write_u32_le(file, (uint32_t)encoded->channels)
        || !dic_roi_write_u32_le(file, (uint32_t)encoded->levels)
        || !dic_roi_write_u32_le(file, (uint32_t)encoded->quant_step)
        || !dic_roi_write_u32_le(file, (uint32_t)encoded->roi_shift)
        || !dic_roi_write_u32_le(file, (uint32_t)encoded->roi_rect.x)
        || !dic_roi_write_u32_le(file, (uint32_t)encoded->roi_rect.y)
        || !dic_roi_write_u32_le(file, (uint32_t)encoded->roi_rect.width)
        || !dic_roi_write_u32_le(file, (uint32_t)encoded->roi_rect.height))
    {
        fclose(file);
        return DIC_STATUS_IO_ERROR;
    }

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        const dic_bitplane_stream *stream = &encoded->channel_streams[channel].bitplanes;
        size_t byte_count = (stream->bit_count + 7u) / 8u;

        if (stream->coefficient_count > (size_t)UINT32_MAX
            || stream->bit_count > (size_t)UINT32_MAX
            || stream->max_bitplanes < 0
            || stream->max_bitplanes > 32
            || !dic_roi_write_u32_le(file, (uint32_t)stream->coefficient_count)
            || !dic_roi_write_u32_le(file, (uint32_t)stream->max_bitplanes)
            || !dic_roi_write_u32_le(file, (uint32_t)stream->bit_count)
            || (byte_count > 0u && fwrite(stream->bytes, 1u, byte_count, file) != byte_count))
        {
            status = DIC_STATUS_IO_ERROR;
            break;
        }
    }

    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Principle: ROI decoding requires the same ROI rectangle and shift metadata used during Maxshift encoding:
 * https://www.sciencedirect.com/science/article/pii/S0923596501000261 */
dic_status dic_roi_read_file(
    const char *path,
    dic_roi_encoded_image *encoded
)
{
    FILE *file = NULL;
    char magic[4];
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t levels;
    uint32_t quant_step;
    uint32_t roi_shift;
    uint32_t roi_x;
    uint32_t roi_y;
    uint32_t roi_width;
    uint32_t roi_height;
    dic_status status = DIC_STATUS_OK;
    int channel;

    if (path == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_roi_encoded_free(encoded);
    file = dic_roi_open_file(path, "rb");
    if (file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;

    if (fread(magic, 1u, sizeof(magic), file) != sizeof(magic)
        || memcmp(magic, DIC_ROI_FILE_MAGIC, sizeof(magic)) != 0
        || !dic_roi_read_u32_le(file, &version)
        || !dic_roi_read_u32_le(file, &width)
        || !dic_roi_read_u32_le(file, &height)
        || !dic_roi_read_u32_le(file, &channels)
        || !dic_roi_read_u32_le(file, &levels)
        || !dic_roi_read_u32_le(file, &quant_step)
        || !dic_roi_read_u32_le(file, &roi_shift)
        || !dic_roi_read_u32_le(file, &roi_x)
        || !dic_roi_read_u32_le(file, &roi_y)
        || !dic_roi_read_u32_le(file, &roi_width)
        || !dic_roi_read_u32_le(file, &roi_height))
    {
        fclose(file);
        return DIC_HW4_FORMAT_ERROR;
    }

    if (version != DIC_ROI_FILE_VERSION
        || width == 0u
        || height == 0u
        || (channels != 1u && channels != 3u)
        || levels == 0u
        || quant_step == 0u
        || roi_shift > 16u
        || roi_width == 0u
        || roi_height == 0u
        || roi_x + roi_width > width
        || roi_y + roi_height > height)
    {
        fclose(file);
        return DIC_HW4_FORMAT_ERROR;
    }

    encoded->channel_streams = (dic_roi_channel_stream *)calloc(
        (size_t)channels,
        sizeof(encoded->channel_streams[0])
    );
    if (encoded->channel_streams == NULL)
    {
        fclose(file);
        return DIC_STATUS_MEMORY_ERROR;
    }

    encoded->width = (int)width;
    encoded->height = (int)height;
    encoded->channels = (int)channels;
    encoded->levels = (int)levels;
    encoded->quant_step = (int)quant_step;
    encoded->roi_shift = (int)roi_shift;
    encoded->roi_rect.x = (int)roi_x;
    encoded->roi_rect.y = (int)roi_y;
    encoded->roi_rect.width = (int)roi_width;
    encoded->roi_rect.height = (int)roi_height;

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        uint32_t coefficient_count;
        uint32_t max_bitplanes;
        uint32_t bit_count;
        size_t byte_count;
        dic_bitplane_stream *stream = &encoded->channel_streams[channel].bitplanes;

        if (!dic_roi_read_u32_le(file, &coefficient_count)
            || !dic_roi_read_u32_le(file, &max_bitplanes)
            || !dic_roi_read_u32_le(file, &bit_count)
            || coefficient_count != width * height
            || max_bitplanes > 32u)
        {
            status = DIC_HW4_FORMAT_ERROR;
            break;
        }

        byte_count = ((size_t)bit_count + 7u) / 8u;
        stream->coefficient_count = (size_t)coefficient_count;
        stream->max_bitplanes = (int)max_bitplanes;
        stream->bit_count = (size_t)bit_count;
        stream->bytes = byte_count > 0u ? (unsigned char *)malloc(byte_count) : NULL;
        if (stream->bytes == NULL && byte_count > 0u)
        {
            status = DIC_STATUS_MEMORY_ERROR;
            break;
        }
        if (byte_count > 0u && fread(stream->bytes, 1u, byte_count, file) != byte_count)
        {
            status = DIC_HW4_FORMAT_ERROR;
            break;
        }
    }

    if (status == DIC_STATUS_OK && fgetc(file) != EOF)
        status = DIC_HW4_FORMAT_ERROR;

    fclose(file);
    if (status != DIC_STATUS_OK)
        dic_roi_encoded_free(encoded);
    return status;
}

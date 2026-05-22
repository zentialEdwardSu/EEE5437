#include "codec/dic_snr_codec.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec/dic_predict.h"
#include "codec/dic_quant.h"
#include "codec/dic_subband.h"
#include "wavelet/dic_dwt53.h"

static FILE *dic_snr_open_file(const char *path, const char *mode)
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

static int dic_snr_write_u32_le(FILE *file, uint32_t value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24) & 0xffu);
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes);
}

static int dic_snr_read_u32_le(FILE *file, uint32_t *value)
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

void dic_snr_encoded_init(dic_snr_encoded_image *encoded)
{
    if (encoded == NULL)
        return;

    encoded->width = 0;
    encoded->height = 0;
    encoded->channels = 0;
    encoded->levels = 0;
    encoded->quant_step = 0;
    encoded->channel_streams = NULL;
}

void dic_snr_encoded_free(dic_snr_encoded_image *encoded)
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
    dic_snr_encoded_init(encoded);
}

static dic_status dic_snr_validate_params(
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

static void dic_snr_copy_channel_to_plane(
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

static uint8_t dic_snr_clamp_u8(int32_t value)
{
    if (value < 0)
        return 0u;
    if (value > 255)
        return 255u;
    return (uint8_t)value;
}

static void dic_snr_copy_plane_to_channel(
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
                dic_snr_clamp_u8(plane[pixel]);
        }
    }
}

dic_status dic_snr_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int levels,
    int quant_step,
    dic_snr_encoded_image *encoded
)
{
    size_t plane_count;
    int32_t *plane = NULL;
    dic_rect_i32 ll_rect;
    dic_status status;
    int channel;

    if (input == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_snr_validate_params(width, height, channels, levels, quant_step);
    if (status != DIC_STATUS_OK)
        return status;

    plane_count = (size_t)width * (size_t)height;
    plane = (int32_t *)malloc(plane_count * sizeof(plane[0]));
    if (plane == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    dic_snr_encoded_free(encoded);
    encoded->channel_streams = (dic_snr_channel_stream *)calloc(
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

    status = dic_subband_lowest_ll_rect(width, height, levels, &ll_rect);
    if (status != DIC_STATUS_OK)
    {
        free(plane);
        dic_snr_encoded_free(encoded);
        return status;
    }

    for (channel = 0; channel < channels; ++channel)
    {
        dic_snr_copy_channel_to_plane(input, width, height, channels, channel, plane);

        status = dic_dwt53_forward_plane(plane, width, height, levels);
        if (status == DIC_STATUS_OK)
            status = dic_quant_scalar_i32(plane, plane_count, quant_step);
        if (status == DIC_STATUS_OK)
            status = dic_predict_ll_left(plane, width, ll_rect);
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
            dic_snr_encoded_free(encoded);
            return status;
        }
    }

    free(plane);
    return DIC_STATUS_OK;
}

dic_status dic_snr_decode_image(
    const dic_snr_encoded_image *encoded,
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

    status = dic_snr_validate_params(
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

        dic_snr_copy_plane_to_channel(
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

dic_status dic_snr_write_file(
    const char *path,
    const dic_snr_encoded_image *encoded
)
{
    FILE *file = NULL;
    dic_status status = DIC_STATUS_OK;
    int channel;

    if (path == NULL || encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = dic_snr_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (fwrite(DIC_SNR_FILE_MAGIC, 1u, 4u, file) != 4u
        || !dic_snr_write_u32_le(file, DIC_SNR_FILE_VERSION)
        || !dic_snr_write_u32_le(file, (uint32_t)encoded->width)
        || !dic_snr_write_u32_le(file, (uint32_t)encoded->height)
        || !dic_snr_write_u32_le(file, (uint32_t)encoded->channels)
        || !dic_snr_write_u32_le(file, (uint32_t)encoded->levels)
        || !dic_snr_write_u32_le(file, (uint32_t)encoded->quant_step))
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
            || !dic_snr_write_u32_le(file, (uint32_t)stream->coefficient_count)
            || !dic_snr_write_u32_le(file, (uint32_t)stream->max_bitplanes)
            || !dic_snr_write_u32_le(file, (uint32_t)stream->bit_count)
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

dic_status dic_snr_read_file(
    const char *path,
    dic_snr_encoded_image *encoded
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
    dic_status status = DIC_STATUS_OK;
    int channel;

    if (path == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_snr_encoded_free(encoded);
    file = dic_snr_open_file(path, "rb");
    if (file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;

    if (fread(magic, 1u, sizeof(magic), file) != sizeof(magic)
        || memcmp(magic, DIC_SNR_FILE_MAGIC, sizeof(magic)) != 0
        || !dic_snr_read_u32_le(file, &version)
        || !dic_snr_read_u32_le(file, &width)
        || !dic_snr_read_u32_le(file, &height)
        || !dic_snr_read_u32_le(file, &channels)
        || !dic_snr_read_u32_le(file, &levels)
        || !dic_snr_read_u32_le(file, &quant_step))
    {
        fclose(file);
        return DIC_HW4_FORMAT_ERROR;
    }

    if (version != DIC_SNR_FILE_VERSION
        || width == 0u
        || height == 0u
        || (channels != 1u && channels != 3u)
        || levels == 0u
        || quant_step == 0u)
    {
        fclose(file);
        return DIC_HW4_FORMAT_ERROR;
    }

    encoded->channel_streams = (dic_snr_channel_stream *)calloc(
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

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        uint32_t coefficient_count;
        uint32_t max_bitplanes;
        uint32_t bit_count;
        size_t byte_count;
        dic_bitplane_stream *stream = &encoded->channel_streams[channel].bitplanes;

        if (!dic_snr_read_u32_le(file, &coefficient_count)
            || !dic_snr_read_u32_le(file, &max_bitplanes)
            || !dic_snr_read_u32_le(file, &bit_count)
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
        dic_snr_encoded_free(encoded);
    return status;
}

size_t dic_snr_layer_bit_count(
    const dic_snr_encoded_image *encoded,
    int decoded_bitplanes
)
{
    size_t total = 0u;
    int channel;

    if (encoded == NULL || encoded->channel_streams == NULL)
        return 0u;

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        size_t bits = 0u;
        if (dic_bitplane_prefix_bit_count(
                &encoded->channel_streams[channel].bitplanes,
                decoded_bitplanes,
                &bits) != DIC_STATUS_OK)
        {
            return 0u;
        }
        total += bits;
    }

    return total;
}

int dic_snr_max_bitplanes(const dic_snr_encoded_image *encoded)
{
    int max_bitplanes = 0;
    int channel;

    if (encoded == NULL || encoded->channel_streams == NULL)
        return 0;

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        int bits = encoded->channel_streams[channel].bitplanes.max_bitplanes;
        if (bits > max_bitplanes)
            max_bitplanes = bits;
    }

    return max_bitplanes;
}

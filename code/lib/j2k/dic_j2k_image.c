/**
 * @file dic_j2k_image.c
 * @brief Converts project images into the constrained JPEG 2000 path described by Annexes B, D, F, G, and I.
 *
 * This file tiles image data, applies the reversible component transform when applicable,
 * performs the project 5-3 wavelet decomposition, partitions sub-bands into code-blocks,
 * EBCOT-encodes them, and writes either a raw codestream or JP2 file. It is not a full
 * Part 1 encoder: rate allocation, precinct progression variation, and optional coding
 * styles are deliberately fixed to the local testable subset.
 *
 * References: image_u8 for input ownership, dic_j2k_layout.c for Annex B geometry,
 * dic_j2k_rct.c for Annex G RCT, dic_j2k_ebcot.c for Annex D coding, dic_j2k_codestream.c
 * and dic_jp2_file.c for output syntax, plus Annex J.3-J.5 sample transform material.
 */

#include "j2k/dic_j2k_image.h"
#include "j2k/dic_j2k_debug.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "codec/dic_subband.h"
#include "j2k/dic_j2k_codestream.h"
#include "j2k/dic_j2k_packet.h"
#include "j2k/dic_j2k_rct.h"
#include "j2k/dic_jp2_file.h"
#include "wavelet/dic_dwt53.h"

enum
{
    DIC_J2K_IMAGE_CODEBLOCK_SIZE = 64
};

typedef struct dic_j2k_image_payload
{
    uint8_t *data;
    size_t size;
    size_t capacity;
} dic_j2k_image_payload;

typedef struct dic_j2k_image_stream_list
{
    dic_j2k_codeblock_stream *streams;
    size_t count;
    size_t capacity;
} dic_j2k_image_stream_list;

static void dic_j2k_image_payload_init(dic_j2k_image_payload *payload)
{
    DIC_J2K_DEBUG_ENTER();
    payload->data = NULL;
    payload->size = 0u;
    payload->capacity = 0u;
}

static void dic_j2k_image_payload_free(dic_j2k_image_payload *payload)
{
    DIC_J2K_DEBUG_ENTER();
    free(payload->data);
    dic_j2k_image_payload_init(payload);
}

static void dic_j2k_image_stream_list_init(dic_j2k_image_stream_list *list)
{
    DIC_J2K_DEBUG_ENTER();
    list->streams = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static void dic_j2k_image_stream_list_free(dic_j2k_image_stream_list *list)
{
    DIC_J2K_DEBUG_ENTER();
    while (list->capacity > 0u)
    {
        --list->capacity;
        dic_j2k_codeblock_stream_free(list->streams + list->capacity);
    }
    free(list->streams);
    dic_j2k_image_stream_list_init(list);
}

static dic_status dic_j2k_image_payload_append(
    dic_j2k_image_payload *payload,
    const uint8_t *data,
    size_t size
)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t *new_data;
    size_t new_capacity;

    if (size == 0u)
        return DIC_STATUS_OK;
    if (data == NULL || size > (size_t)-1 - payload->size)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (payload->size + size > payload->capacity)
    {
        new_capacity = payload->capacity == 0u ? 256u : payload->capacity;
        while (new_capacity < payload->size + size)
        {
            if (new_capacity > (size_t)-1 / 2u)
                return DIC_STATUS_INVALID_ARGUMENT;
            new_capacity *= 2u;
        }
        new_data = (uint8_t *)realloc(payload->data, new_capacity);
        if (new_data == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        payload->data = new_data;
        payload->capacity = new_capacity;
    }

    memcpy(payload->data + payload->size, data, size);
    payload->size += size;
    return DIC_STATUS_OK;
}

static dic_status dic_j2k_image_stream_list_push_empty(
    dic_j2k_image_stream_list *list,
    dic_j2k_codeblock_stream **stream
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_codeblock_stream *new_streams;
    size_t new_capacity;

    if (list == NULL || stream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    if (list->count == list->capacity)
    {
        new_capacity = list->capacity == 0u ? 8u : list->capacity * 2u;
        if (new_capacity < list->capacity)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_streams = (dic_j2k_codeblock_stream *)realloc(
            list->streams,
            new_capacity * sizeof(list->streams[0])
        );
        if (new_streams == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        list->streams = new_streams;
        for (; list->capacity < new_capacity; ++list->capacity)
            dic_j2k_codeblock_stream_init(list->streams + list->capacity);
    }

    *stream = list->streams + list->count;
    ++list->count;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G.1 Figures G.1-G.2, unsigned 8-bit tile-components are DC level shifted before DWT. */
static int32_t dic_j2k_image_level_shift(uint8_t sample)
{
    DIC_J2K_DEBUG_ENTER();
    return (int32_t)sample - 128;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex F.4, FDWT is iterated over the LL region only; tiny images therefore use fewer levels. */
static int dic_j2k_image_effective_levels(int width, int height, int requested_levels)
{
    DIC_J2K_DEBUG_ENTER();
    int levels = 0;

    while (levels < requested_levels && width >= 2 && height >= 2)
    {
        ++levels;
        width = dic_dwt53_low_size(width);
        height = dic_dwt53_low_size(height);
    }

    return levels;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G.1-G.2, unsigned samples are level shifted and RGB input uses the reversible component transform when COD MCT is set. */
static dic_status dic_j2k_image_make_planes(
    const dic_image_u8 *image,
    int32_t **planes_out
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t pixel_count;
    int32_t *planes;
    size_t pixel;
    int component;

    if (image == NULL || planes_out == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (image->width <= 0 || image->height <= 0 || (image->channels != 1 && image->channels != 3))
        return DIC_STATUS_INVALID_ARGUMENT;

    pixel_count = (size_t)image->width * (size_t)image->height;
    if (pixel_count > (size_t)-1 / (size_t)image->channels / sizeof(planes[0]))
        return DIC_STATUS_INVALID_ARGUMENT;

    planes = (int32_t *)malloc(pixel_count * (size_t)image->channels * sizeof(planes[0]));
    if (planes == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    if (image->channels == 1)
    {
        for (pixel = 0u; pixel < pixel_count; ++pixel)
            planes[pixel] = dic_j2k_image_level_shift(image->data[pixel]);
    }
    else
    {
        int32_t *interleaved = (int32_t *)malloc(pixel_count * 3u * sizeof(interleaved[0]));
        dic_status status;

        if (interleaved == NULL)
        {
            free(planes);
            return DIC_STATUS_MEMORY_ERROR;
        }
        for (pixel = 0u; pixel < pixel_count; ++pixel)
        {
            for (component = 0; component < 3; ++component)
                interleaved[pixel * 3u + (size_t)component] =
                    dic_j2k_image_level_shift(image->data[pixel * 3u + (size_t)component]);
        }

        status = dic_j2k_rct_forward(interleaved, pixel_count);
        if (status != DIC_STATUS_OK)
        {
            free(interleaved);
            free(planes);
            return status;
        }

        for (pixel = 0u; pixel < pixel_count; ++pixel)
        {
            for (component = 0; component < 3; ++component)
                planes[(size_t)component * pixel_count + pixel] =
                    interleaved[pixel * 3u + (size_t)component];
        }
        free(interleaved);
    }

    *planes_out = planes;
    return DIC_STATUS_OK;
}

static dic_j2k_subband_orientation dic_j2k_image_orientation(dic_subband_orientation orientation)
{
    DIC_J2K_DEBUG_ENTER();
    if (orientation == DIC_SUBBAND_HL)
        return DIC_J2K_SUBBAND_HL;
    if (orientation == DIC_SUBBAND_HH)
        return DIC_J2K_SUBBAND_HH;
    return DIC_J2K_SUBBAND_LL_LH;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.7, sub-bands are partitioned into rectangular code-blocks anchored on the code-block grid. */
static dic_status dic_j2k_image_append_subband_streams(
    const int32_t *plane,
    int plane_width,
    const dic_rect_i32 *rect,
    dic_j2k_subband_orientation orientation,
    uint32_t nominal_bitplanes,
    dic_j2k_image_stream_list *streams
)
{
    DIC_J2K_DEBUG_ENTER();
    int by;
    dic_status status = DIC_STATUS_OK;

    if (plane == NULL || rect == NULL || streams == NULL || rect->width <= 0 || rect->height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (by = 0; by < rect->height; by += DIC_J2K_IMAGE_CODEBLOCK_SIZE)
    {
        int bx;
        int block_height = rect->height - by < DIC_J2K_IMAGE_CODEBLOCK_SIZE
            ? rect->height - by
            : DIC_J2K_IMAGE_CODEBLOCK_SIZE;

        for (bx = 0; bx < rect->width; bx += DIC_J2K_IMAGE_CODEBLOCK_SIZE)
        {
            int block_width = rect->width - bx < DIC_J2K_IMAGE_CODEBLOCK_SIZE
                ? rect->width - bx
                : DIC_J2K_IMAGE_CODEBLOCK_SIZE;
            int32_t *block = NULL;
            dic_j2k_codeblock_stream *stream = NULL;
            int y;

            status = dic_j2k_image_stream_list_push_empty(streams, &stream);
            if (status != DIC_STATUS_OK)
                break;

            block = (int32_t *)malloc((size_t)block_width * (size_t)block_height * sizeof(block[0]));
            if (block == NULL)
            {
                status = DIC_STATUS_MEMORY_ERROR;
                break;
            }
            for (y = 0; y < block_height; ++y)
            {
                memcpy(
                    block + (size_t)y * (size_t)block_width,
                    plane + (size_t)(rect->y + by + y) * (size_t)plane_width + (size_t)(rect->x + bx),
                    (size_t)block_width * sizeof(block[0])
                );
            }
            status = dic_j2k_ebcot_encode_codeblock_rect(
                block,
                (uint32_t)block_width,
                (uint32_t)block_height,
                orientation,
                stream
            );
            /* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.5 and Annex E.1.1, zero bit-planes are counted relative to the sub-band exponent advertised by QCD. */
            if (status == DIC_STATUS_OK)
            {
                stream->zero_bitplanes = nominal_bitplanes > stream->magnitude_bitplanes
                    ? nominal_bitplanes - stream->magnitude_bitplanes
                    : 0u;
            }
            free(block);
            if (status != DIC_STATUS_OK)
                break;
        }
        if (status != DIC_STATUS_OK)
            break;
    }

    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.8, one packet of a resolution/component contains LL or the ordered HL, LH, HH sub-band contributions. */
static dic_status dic_j2k_image_append_resolution_packet(
    int32_t *plane,
    int width,
    int height,
    int levels,
    int resolution,
    uint32_t component_extra_bits,
    dic_j2k_image_payload *payload
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_image_stream_list subband_streams[3];
    dic_j2k_packet_subband_payload subbands[3];
    size_t subband_count = 0u;
    size_t subband_index;
    dic_j2k_packet_header packet;
    dic_status status = DIC_STATUS_OK;

    for (subband_index = 0u; subband_index < 3u; ++subband_index)
    {
        dic_j2k_image_stream_list_init(subband_streams + subband_index);
        memset(subbands + subband_index, 0, sizeof(subbands[subband_index]));
    }
    dic_j2k_packet_header_init(&packet);

    if (resolution == 0)
    {
        dic_rect_i32 rect;

        if (levels == 0)
        {
            rect.x = 0;
            rect.y = 0;
            rect.width = width;
            rect.height = height;
        }
        else
        {
            status = dic_subband_lowest_ll_rect(width, height, levels, &rect);
        }
        if (status == DIC_STATUS_OK)
        {
            status = dic_j2k_image_append_subband_streams(
                plane,
                width,
                &rect,
                DIC_J2K_SUBBAND_LL_LH,
                9u + component_extra_bits,
                subband_streams
            );
            if (status == DIC_STATUS_OK)
            {
                subbands[0].streams = subband_streams[0].streams;
                subbands[0].stream_count = subband_streams[0].count;
                subbands[0].blocks_x = (rect.width + DIC_J2K_IMAGE_CODEBLOCK_SIZE - 1) / DIC_J2K_IMAGE_CODEBLOCK_SIZE;
                subbands[0].blocks_y = (rect.height + DIC_J2K_IMAGE_CODEBLOCK_SIZE - 1) / DIC_J2K_IMAGE_CODEBLOCK_SIZE;
                subband_count = 1u;
            }
        }
    }
    else
    {
        static const dic_subband_orientation orientations[] = {
            DIC_SUBBAND_HL,
            DIC_SUBBAND_LH,
            DIC_SUBBAND_HH
        };
        size_t index;
        int level = levels - resolution + 1;

        for (index = 0u; index < sizeof(orientations) / sizeof(orientations[0]); ++index)
        {
            dic_rect_i32 rect;

            status = dic_subband_rect(width, height, levels, level, orientations[index], &rect);
            if (status != DIC_STATUS_OK)
                break;
            status = dic_j2k_image_append_subband_streams(
                plane,
                width,
                &rect,
                dic_j2k_image_orientation(orientations[index]),
                (orientations[index] == DIC_SUBBAND_HH ? 11u : 10u) + component_extra_bits,
                subband_streams + index
            );
            if (status != DIC_STATUS_OK)
                break;
            subbands[index].streams = subband_streams[index].streams;
            subbands[index].stream_count = subband_streams[index].count;
            subbands[index].blocks_x = (rect.width + DIC_J2K_IMAGE_CODEBLOCK_SIZE - 1) / DIC_J2K_IMAGE_CODEBLOCK_SIZE;
            subbands[index].blocks_y = (rect.height + DIC_J2K_IMAGE_CODEBLOCK_SIZE - 1) / DIC_J2K_IMAGE_CODEBLOCK_SIZE;
            subband_count = index + 1u;
        }
    }

    if (status == DIC_STATUS_OK)
    {
        status = dic_j2k_packet_build_tagged_ebcot_payload(subbands, subband_count, &packet);
        if (status == DIC_STATUS_OK)
            status = dic_j2k_image_payload_append(payload, packet.data, packet.size);
    }

    dic_j2k_packet_header_free(&packet);
    for (subband_index = 0u; subband_index < 3u; ++subband_index)
        dic_j2k_image_stream_list_free(subband_streams + subband_index);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.8, LRCP packet order visits resolution levels, components, then precinct data. */
static dic_status dic_j2k_image_build_payload(
    int32_t *planes,
    int width,
    int height,
    int channels,
    int levels,
    dic_j2k_image_payload *payload
)
{
    DIC_J2K_DEBUG_ENTER();
    int resolution;
    int component;
    dic_status status = DIC_STATUS_OK;
    size_t plane_samples = (size_t)width * (size_t)height;

    for (resolution = 0; resolution <= levels && status == DIC_STATUS_OK; ++resolution)
    {
        for (component = 0; component < channels && status == DIC_STATUS_OK; ++component)
        {
            int32_t *plane = planes + (size_t)component * plane_samples;
            uint32_t component_extra_bits = channels == 3 && component > 0 ? 1u : 0u;

            status = dic_j2k_image_append_resolution_packet(
                plane,
                width,
                height,
                levels,
                resolution,
                component_extra_bits,
                payload
            );
        }
    }

    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex F.4 and Annex B.10, transformed tile-components are entropy coded into packetized code-block contributions. */
static dic_status dic_j2k_image_encode_payload(
    const dic_image_u8 *image,
    int requested_levels,
    dic_j2k_basic_params *params,
    dic_j2k_image_payload *payload
)
{
    DIC_J2K_DEBUG_ENTER();
    int32_t *planes = NULL;
    dic_status status;
    int levels;
    int component;
    size_t plane_samples;

    if (image == NULL || params == NULL || payload == NULL || requested_levels < 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (image->width <= 0 || image->height <= 0 || (image->channels != 1 && image->channels != 3))
        return DIC_STATUS_INVALID_ARGUMENT;

    levels = dic_j2k_image_effective_levels(image->width, image->height, requested_levels);
    status = dic_j2k_image_make_planes(image, &planes);
    if (status != DIC_STATUS_OK)
        return status;

    plane_samples = (size_t)image->width * (size_t)image->height;
    for (component = 0; component < image->channels; ++component)
    {
        if (levels > 0)
        {
            status = dic_dwt53_forward_plane(
                planes + (size_t)component * plane_samples,
                image->width,
                image->height,
                levels
            );
            if (status != DIC_STATUS_OK)
                break;
        }
    }

    if (status == DIC_STATUS_OK)
        status = dic_j2k_image_build_payload(
            planes,
            image->width,
            image->height,
            image->channels,
            levels,
            payload
        );

    if (status == DIC_STATUS_OK)
    {
        params->width = (uint32_t)image->width;
        params->height = (uint32_t)image->height;
        params->components = (uint16_t)image->channels;
        params->decomposition_levels = (uint8_t)levels;
        params->reversible = 1u;
        params->multiple_component_transform = image->channels == 3 ? 1u : 0u;
        params->layers = 1u;
    }

    free(planes);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A.3-A.4, a raw codestream is main header, one tile-part SOD payload, and EOC. */
dic_status dic_j2k_write_image_codestream(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_basic_params params = {0};
    dic_j2k_image_payload payload;
    dic_status status;

    dic_j2k_image_payload_init(&payload);
    if (path == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_j2k_image_encode_payload(image, requested_levels, &params, &payload);
    if (status == DIC_STATUS_OK)
        status = dic_j2k_write_codestream_with_payload(path, &params, payload.data, payload.size);

    dic_j2k_image_payload_free(&payload);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1, JP2 stores the same codestream in a Contiguous Codestream box. */
dic_status dic_j2k_write_image_jp2(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_basic_params params = {0};
    dic_j2k_image_payload payload;
    dic_status status;

    dic_j2k_image_payload_init(&payload);
    if (path == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_j2k_image_encode_payload(image, requested_levels, &params, &payload);
    if (status == DIC_STATUS_OK)
        status = dic_jp2_write_file_with_codestream_payload(path, &params, payload.data, payload.size);

    dic_j2k_image_payload_free(&payload);
    return status;
}

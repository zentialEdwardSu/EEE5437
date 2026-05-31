/**
 * @file j2k_image.c
 * @brief Converts project images into the constrained JPEG 2000 path described by Annexes B, D, F, G, and I.
 *
 * This file tiles image data, applies the reversible component transform when applicable,
 * performs the project 5-3 wavelet decomposition, partitions sub-bands into code-blocks,
 * EBCOT-encodes them, and writes either a raw codestream or JP2 file. It is not a full
 * Part 1 encoder: precinct progression variation and optional coding styles are
 * deliberately fixed to the local testable subset, while quality layers use the
 * EBCOT pass-level rate-distortion metadata maintained by j2k_packet.c.
 *
 * References: image_u8 for input ownership, j2k_layout.c for Annex B geometry,
 * j2k_rct.c for Annex G RCT, j2k_ebcot.c for Annex D coding, j2k_codestream.c
 * and jp2_file.c for output syntax, plus Annex J.3-J.5 sample transform material.
 */

#include "j2k/j2k_image.h"
#include "j2k/j2k_debug.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "codec/dic_subband.h"
#include "j2k/j2k_codestream.h"
#include "j2k/j2k_ict.h"
#include "j2k/j2k_packet.h"
#include "j2k/j2k_quant.h"
#include "j2k/j2k_rct.h"
#include "j2k/j2k_roi.h"
#include "j2k/jp2_file.h"
#include "wavelet/dic_dwt53.h"
#include "wavelet/dic_dwt97.h"

enum
{
    j2k_IMAGE_CODEBLOCK_SIZE = 64,
    j2k_IMAGE_LOSSY_GUARD_BITS = 7
};

typedef struct j2k_image_payload
{
    uint8_t *data;
    size_t size;
    size_t capacity;
} j2k_image_payload;

typedef struct j2k_image_stream_list
{
    j2k_codeblock_stream *streams;
    size_t count;
    size_t capacity;
} j2k_image_stream_list;

typedef struct j2k_image_tile_payloads
{
    j2k_tile_part_payload *tile_parts;
    j2k_image_payload *payloads;
    size_t count;
} j2k_image_tile_payloads;

static void j2k_image_payload_init(j2k_image_payload *payload)
{
    j2k_DEBUG_ENTER();
    payload->data = NULL;
    payload->size = 0u;
    payload->capacity = 0u;
}

static void j2k_image_payload_free(j2k_image_payload *payload)
{
    j2k_DEBUG_ENTER();
    free(payload->data);
    j2k_image_payload_init(payload);
}

static void j2k_image_stream_list_init(j2k_image_stream_list *list)
{
    j2k_DEBUG_ENTER();
    list->streams = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static void j2k_image_stream_list_free(j2k_image_stream_list *list)
{
    j2k_DEBUG_ENTER();
    while (list->capacity > 0u)
    {
        --list->capacity;
        j2k_codeblock_stream_free(list->streams + list->capacity);
    }
    free(list->streams);
    j2k_image_stream_list_init(list);
}

static void j2k_image_tile_payloads_init(j2k_image_tile_payloads *tile_payloads)
{
    j2k_DEBUG_ENTER();
    tile_payloads->tile_parts = NULL;
    tile_payloads->payloads = NULL;
    tile_payloads->count = 0u;
}

static void j2k_image_tile_payloads_free(j2k_image_tile_payloads *tile_payloads)
{
    j2k_DEBUG_ENTER();
    size_t index;

    if (tile_payloads == NULL)
        return;
    for (index = 0u; index < tile_payloads->count; ++index)
        j2k_image_payload_free(tile_payloads->payloads + index);
    free(tile_payloads->payloads);
    free(tile_payloads->tile_parts);
    j2k_image_tile_payloads_init(tile_payloads);
}

static dic_status j2k_image_payload_append(
    j2k_image_payload *payload,
    const uint8_t *data,
    size_t size
)
{
    j2k_DEBUG_ENTER();
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

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A marker fields are emitted most-significant byte first. */
static dic_status j2k_image_payload_append_u16_be(
    j2k_image_payload *payload,
    uint16_t value
)
{
    j2k_DEBUG_ENTER();
    uint8_t bytes[2];

    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)(value & 0xffu);
    return j2k_image_payload_append(payload, bytes, sizeof(bytes));
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.8 and B.10, SOP/EPH markers wrap packet headers without changing code-block bytes. */
static dic_status j2k_image_payload_append_packet(
    j2k_image_payload *payload,
    const j2k_packet_header *packet,
    size_t packet_header_size,
    uint16_t packet_sequence
)
{
    j2k_DEBUG_ENTER();
    dic_status status;

    if (payload == NULL || packet == NULL || packet_header_size > packet->size)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = j2k_image_payload_append_u16_be(payload, j2k_MARKER_SOP);
    if (status == DIC_STATUS_OK)
        status = j2k_image_payload_append_u16_be(payload, 4u);
    if (status == DIC_STATUS_OK)
        status = j2k_image_payload_append_u16_be(payload, packet_sequence);
    if (status == DIC_STATUS_OK)
        status = j2k_image_payload_append(payload, packet->data, packet_header_size);
    if (status == DIC_STATUS_OK)
        status = j2k_image_payload_append_u16_be(payload, j2k_MARKER_EPH);
    if (status == DIC_STATUS_OK)
    {
        size_t packet_body_size = packet->size - packet_header_size;

        if (packet_body_size > 0u)
        {
            status = j2k_image_payload_append(
                payload,
                packet->data + packet_header_size,
                packet_body_size
            );
        }
    }
    return status;
}

static dic_status j2k_image_stream_list_push_empty(
    j2k_image_stream_list *list,
    j2k_codeblock_stream **stream
)
{
    j2k_DEBUG_ENTER();
    j2k_codeblock_stream *new_streams;
    size_t new_capacity;

    if (list == NULL || stream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    if (list->count == list->capacity)
    {
        new_capacity = list->capacity == 0u ? 8u : list->capacity * 2u;
        if (new_capacity < list->capacity)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_streams = (j2k_codeblock_stream *)realloc(
            list->streams,
            new_capacity * sizeof(list->streams[0])
        );
        if (new_streams == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        list->streams = new_streams;
        for (; list->capacity < new_capacity; ++list->capacity)
            j2k_codeblock_stream_init(list->streams + list->capacity);
    }

    *stream = list->streams + list->count;
    ++list->count;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G.1 Figures G.1-G.2, unsigned 8-bit tile-components are DC level shifted before DWT. */
static int32_t j2k_image_level_shift(uint8_t sample)
{
    j2k_DEBUG_ENTER();
    return (int32_t)sample - 128;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex F.4, FDWT is iterated over the LL region only; tiny images therefore use fewer levels. */
static int j2k_image_effective_levels(int width, int height, int requested_levels)
{
    j2k_DEBUG_ENTER();
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
static dic_status j2k_image_make_planes(
    const dic_image_u8 *image,
    int32_t **planes_out
)
{
    j2k_DEBUG_ENTER();
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
            planes[pixel] = j2k_image_level_shift(image->data[pixel]);
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
                    j2k_image_level_shift(image->data[pixel * 3u + (size_t)component]);
        }

        status = j2k_rct_forward(interleaved, pixel_count);
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

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G.1-G.2, irreversible coding uses the ICT after unsigned sample level shifting. */
static dic_status j2k_image_make_double_planes(
    const dic_image_u8 *image,
    double **planes_out
)
{
    size_t pixel_count;
    double *planes;
    size_t pixel;
    int component;

    if (image == NULL || planes_out == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (image->width <= 0 || image->height <= 0 || (image->channels != 1 && image->channels != 3))
        return DIC_STATUS_INVALID_ARGUMENT;

    pixel_count = (size_t)image->width * (size_t)image->height;
    if (pixel_count > (size_t)-1 / (size_t)image->channels / sizeof(planes[0]))
        return DIC_STATUS_INVALID_ARGUMENT;

    planes = (double *)malloc(pixel_count * (size_t)image->channels * sizeof(planes[0]));
    if (planes == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    if (image->channels == 1)
    {
        for (pixel = 0u; pixel < pixel_count; ++pixel)
            planes[pixel] = (double)j2k_image_level_shift(image->data[pixel]);
    }
    else
    {
        double *interleaved = (double *)malloc(pixel_count * 3u * sizeof(interleaved[0]));
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
                    (double)j2k_image_level_shift(image->data[pixel * 3u + (size_t)component]);
        }
        status = j2k_ict_forward(interleaved, pixel_count);
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

static j2k_subband_orientation j2k_image_orientation(dic_subband_orientation orientation)
{
    j2k_DEBUG_ENTER();
    if (orientation == DIC_SUBBAND_HL)
        return j2k_SUBBAND_HL;
    if (orientation == DIC_SUBBAND_HH)
        return j2k_SUBBAND_HH;
    return j2k_SUBBAND_LL_LH;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.1 Table A.21, 15/15 is the explicit maximum precinct size. */
static void j2k_image_set_max_precincts(j2k_basic_params *params)
{
    j2k_DEBUG_ENTER();
    uint8_t resolution;

    params->use_precincts = 1u;
    for (resolution = 0u; resolution <= params->decomposition_levels; ++resolution)
    {
        params->precinct_width_exponents[resolution] = 15u;
        params->precinct_height_exponents[resolution] = 15u;
    }
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.7, sub-bands are partitioned into rectangular code-blocks anchored on the code-block grid. */
static dic_status j2k_image_append_subband_streams(
    const int32_t *plane,
    int plane_width,
    const dic_rect_i32 *rect,
    j2k_subband_orientation orientation,
    uint32_t nominal_bitplanes,
    j2k_image_stream_list *streams
)
{
    j2k_DEBUG_ENTER();
    int by;
    dic_status status = DIC_STATUS_OK;

    if (plane == NULL || rect == NULL || streams == NULL || rect->width <= 0 || rect->height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (by = 0; by < rect->height; by += j2k_IMAGE_CODEBLOCK_SIZE)
    {
        int bx;
        int block_height = rect->height - by < j2k_IMAGE_CODEBLOCK_SIZE
            ? rect->height - by
            : j2k_IMAGE_CODEBLOCK_SIZE;

        for (bx = 0; bx < rect->width; bx += j2k_IMAGE_CODEBLOCK_SIZE)
        {
            int block_width = rect->width - bx < j2k_IMAGE_CODEBLOCK_SIZE
                ? rect->width - bx
                : j2k_IMAGE_CODEBLOCK_SIZE;
            int32_t *block = NULL;
            j2k_codeblock_stream *stream = NULL;
            int y;

            status = j2k_image_stream_list_push_empty(streams, &stream);
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
            status = j2k_ebcot_encode_codeblock_rect(
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
static dic_status j2k_image_append_resolution_packet(
    int32_t *plane,
    int width,
    int height,
    int levels,
    int resolution,
    uint32_t component_extra_bits,
    uint32_t roi_extra_bits,
    const uint32_t *nominal_bitplanes,
    uint16_t layer_index,
    uint16_t layers,
    uint16_t packet_sequence,
    j2k_image_payload *payload
)
{
    j2k_DEBUG_ENTER();
    j2k_image_stream_list subband_streams[3];
    j2k_packet_subband_payload subbands[3];
    size_t subband_count = 0u;
    size_t subband_index;
    j2k_packet_header packet;
    size_t packet_header_size = 0u;
    dic_status status = DIC_STATUS_OK;

    for (subband_index = 0u; subband_index < 3u; ++subband_index)
    {
        j2k_image_stream_list_init(subband_streams + subband_index);
        memset(subbands + subband_index, 0, sizeof(subbands[subband_index]));
    }
    j2k_packet_header_init(&packet);

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
            status = j2k_image_append_subband_streams(
                plane,
                width,
                &rect,
                j2k_SUBBAND_LL_LH,
                nominal_bitplanes == NULL ? 9u + component_extra_bits + roi_extra_bits : nominal_bitplanes[0],
                subband_streams
            );
            if (status == DIC_STATUS_OK)
            {
                subbands[0].streams = subband_streams[0].streams;
                subbands[0].stream_count = subband_streams[0].count;
                subbands[0].blocks_x = (rect.width + j2k_IMAGE_CODEBLOCK_SIZE - 1) / j2k_IMAGE_CODEBLOCK_SIZE;
                subbands[0].blocks_y = (rect.height + j2k_IMAGE_CODEBLOCK_SIZE - 1) / j2k_IMAGE_CODEBLOCK_SIZE;
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
            status = j2k_image_append_subband_streams(
                plane,
                width,
                &rect,
                j2k_image_orientation(orientations[index]),
                nominal_bitplanes == NULL
                    ? (orientations[index] == DIC_SUBBAND_HH ? 11u : 10u) + component_extra_bits + roi_extra_bits
                    : nominal_bitplanes[1 + (resolution - 1) * 3 + (int)index],
                subband_streams + index
            );
            if (status != DIC_STATUS_OK)
                break;
            subbands[index].streams = subband_streams[index].streams;
            subbands[index].stream_count = subband_streams[index].count;
            subbands[index].blocks_x = (rect.width + j2k_IMAGE_CODEBLOCK_SIZE - 1) / j2k_IMAGE_CODEBLOCK_SIZE;
            subbands[index].blocks_y = (rect.height + j2k_IMAGE_CODEBLOCK_SIZE - 1) / j2k_IMAGE_CODEBLOCK_SIZE;
            subband_count = index + 1u;
        }
    }

    if (status == DIC_STATUS_OK)
    {
        status = j2k_packet_build_tagged_ebcot_layer_payload_with_header_size(
            subbands,
            subband_count,
            layer_index,
            layers,
            &packet,
            &packet_header_size
        );
        if (status == DIC_STATUS_OK)
            status = j2k_image_payload_append_packet(payload, &packet, packet_header_size, packet_sequence);
    }

    j2k_packet_header_free(&packet);
    for (subband_index = 0u; subband_index < 3u; ++subband_index)
        j2k_image_stream_list_free(subband_streams + subband_index);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.8, LRCP packet order visits layers, resolution levels, components, then precinct data. */
static dic_status j2k_image_build_payload(
    int32_t *planes,
    int width,
    int height,
    int channels,
    int levels,
    uint16_t layers,
    uint8_t roi_shift,
    const uint32_t *nominal_bitplanes,
    j2k_image_payload *payload
)
{
    j2k_DEBUG_ENTER();
    uint16_t layer;
    int resolution;
    int component;
    dic_status status = DIC_STATUS_OK;
    size_t plane_samples = (size_t)width * (size_t)height;
    uint16_t packet_sequence = 0u;

    if (layers == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (layer = 0u; layer < layers && status == DIC_STATUS_OK; ++layer)
    {
        for (resolution = 0; resolution <= levels && status == DIC_STATUS_OK; ++resolution)
        {
            for (component = 0; component < channels && status == DIC_STATUS_OK; ++component)
            {
                int32_t *plane = planes + (size_t)component * plane_samples;
                uint32_t component_extra_bits = channels == 3 && component > 0 ? 1u : 0u;

                status = j2k_image_append_resolution_packet(
                    plane,
                    width,
                    height,
                    levels,
                    resolution,
                    component_extra_bits,
                    roi_shift,
                    nominal_bitplanes,
                    layer,
                    layers,
                    packet_sequence,
                    payload
                );
                if (status == DIC_STATUS_OK)
                    ++packet_sequence;
            }
        }
    }

    return status;
}

static dic_status j2k_image_quantize_rect(
    const double *source,
    int32_t *target,
    int plane_width,
    const dic_rect_i32 *rect,
    double step_size
)
{
    int y;

    if (source == NULL || target == NULL || rect == NULL || rect->width <= 0 || rect->height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (y = 0; y < rect->height; ++y)
    {
        int x;

        for (x = 0; x < rect->width; ++x)
        {
            size_t offset = (size_t)(rect->y + y) * (size_t)plane_width + (size_t)(rect->x + x);
            dic_status status = j2k_quantize_coefficient(source[offset], step_size, target + offset);

            if (status != DIC_STATUS_OK)
                return status;
        }
    }
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex E, irreversible coefficient quantization is sub-band scalar quantization before EBCOT. */
static dic_status j2k_image_quantize_planes(
    const double *source,
    int32_t *target,
    int width,
    int height,
    int channels,
    int levels,
    const double *steps
)
{
    size_t plane_samples = (size_t)width * (size_t)height;
    int component;

    for (component = 0; component < channels; ++component)
    {
        const double *source_plane = source + (size_t)component * plane_samples;
        int32_t *target_plane = target + (size_t)component * plane_samples;
        int resolution;
        dic_rect_i32 rect;
        dic_status status;

        status = levels == 0
            ? DIC_STATUS_OK
            : dic_subband_lowest_ll_rect(width, height, levels, &rect);
        if (levels == 0)
        {
            rect.x = 0;
            rect.y = 0;
            rect.width = width;
            rect.height = height;
        }
        if (status == DIC_STATUS_OK)
            status = j2k_image_quantize_rect(source_plane, target_plane, width, &rect, steps[0]);
        for (resolution = 1; status == DIC_STATUS_OK && resolution <= levels; ++resolution)
        {
            static const dic_subband_orientation orientations[] = {
                DIC_SUBBAND_HL,
                DIC_SUBBAND_LH,
                DIC_SUBBAND_HH
            };
            int index;
            int level = levels - resolution + 1;

            for (index = 0; status == DIC_STATUS_OK && index < 3; ++index)
            {
                status = dic_subband_rect(width, height, levels, level, orientations[index], &rect);
                if (status == DIC_STATUS_OK)
                {
                    status = j2k_image_quantize_rect(
                        source_plane,
                        target_plane,
                        width,
                        &rect,
                        steps[1 + (resolution - 1) * 3 + index]
                    );
                }
            }
        }
        if (status != DIC_STATUS_OK)
            return status;
    }
    return DIC_STATUS_OK;
}

static unsigned int j2k_image_irreversible_qcd_range_bits(unsigned int step_index)
{
    if (step_index == 0u)
        return 8u;
    return ((step_index - 1u) % 3u) == 2u ? 10u : 9u;
}

static uint32_t j2k_image_lossy_nominal_bitplanes(
    double step_size,
    unsigned int step_index,
    uint8_t guard_bits
)
{
    uint16_t spqcd;
    uint32_t exponent;

    if (j2k_quant_encode_irreversible_spqcd(
            step_size,
            j2k_image_irreversible_qcd_range_bits(step_index),
            &spqcd
        ) != DIC_STATUS_OK)
    {
        return 1u;
    }

    exponent = (uint32_t)(spqcd >> 11);
    return guard_bits > 0u ? exponent + (uint32_t)guard_bits - 1u : exponent;
}

static dic_status j2k_image_encode_lossy_payload(
    const dic_image_u8 *image,
    int requested_levels,
    uint16_t layers,
    int quality,
    j2k_basic_params *params,
    j2k_image_payload *payload
)
{
    double *double_planes = NULL;
    int32_t *quantized_planes = NULL;
    double base_step;
    dic_status status;
    int levels;
    int component;
    size_t plane_samples;
    unsigned int step;
    unsigned int step_count;
    uint32_t nominal_bitplanes[j2k_MAX_QUANT_STEPS];

    if (image == NULL || params == NULL || payload == NULL || requested_levels < 0 || layers == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (image->width <= 0 || image->height <= 0 || (image->channels != 1 && image->channels != 3))
        return DIC_STATUS_INVALID_ARGUMENT;

    status = j2k_quant_base_step_from_quality(quality, &base_step);
    if (status != DIC_STATUS_OK)
        return status;

    levels = j2k_image_effective_levels(image->width, image->height, requested_levels);
    step_count = 1u + 3u * (unsigned int)levels;
    status = j2k_image_make_double_planes(image, &double_planes);
    if (status != DIC_STATUS_OK)
        return status;
    plane_samples = (size_t)image->width * (size_t)image->height;
    for (component = 0; status == DIC_STATUS_OK && component < image->channels; ++component)
    {
        if (levels > 0)
        {
            status = dic_dwt97_forward_plane(
                double_planes + (size_t)component * plane_samples,
                image->width,
                image->height,
                levels
            );
        }
    }
    if (status == DIC_STATUS_OK)
    {
        quantized_planes = (int32_t *)calloc(plane_samples * (size_t)image->channels, sizeof(quantized_planes[0]));
        if (quantized_planes == NULL)
            status = DIC_STATUS_MEMORY_ERROR;
    }
    for (step = 0u; status == DIC_STATUS_OK && step < step_count; ++step)
    {
        params->quant_step_sizes[step] = base_step;
        nominal_bitplanes[step] = j2k_image_lossy_nominal_bitplanes(
            base_step,
            step,
            (uint8_t)j2k_IMAGE_LOSSY_GUARD_BITS
        );
    }
    if (status == DIC_STATUS_OK)
    {
        status = j2k_image_quantize_planes(
            double_planes,
            quantized_planes,
            image->width,
            image->height,
            image->channels,
            levels,
            params->quant_step_sizes
        );
    }
    if (status == DIC_STATUS_OK)
        status = j2k_image_build_payload(
            quantized_planes,
            image->width,
            image->height,
            image->channels,
            levels,
            layers,
            0u,
            nominal_bitplanes,
            payload
        );
    if (status == DIC_STATUS_OK)
    {
        params->width = (uint32_t)image->width;
        params->height = (uint32_t)image->height;
        params->components = (uint16_t)image->channels;
        params->decomposition_levels = (uint8_t)levels;
        params->reversible = 0u;
        params->multiple_component_transform = image->channels == 3 ? 1u : 0u;
        params->layers = layers;
        params->quant_guard_bits = (uint8_t)j2k_IMAGE_LOSSY_GUARD_BITS;
        params->quant_step_count = (uint16_t)step_count;
        params->use_sop = 1u;
        params->use_eph = 1u;
        j2k_image_set_max_precincts(params);
    }

    free(quantized_planes);
    free(double_planes);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.5.1 and Annex B.5, tile-components are coded independently in tile grid order. */
static dic_status j2k_image_copy_tile(
    const dic_image_u8 *image,
    int tile_x,
    int tile_y,
    int tile_width,
    int tile_height,
    dic_image_u8 *tile
)
{
    j2k_DEBUG_ENTER();
    int y;
    dic_status status;

    if (image == NULL || tile == NULL || tile_width <= 0 || tile_height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (tile_x < 0 || tile_y < 0 || tile_x + tile_width > image->width || tile_y + tile_height > image->height)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_image_u8_alloc(tile, tile_width, tile_height, image->channels);
    if (status != DIC_STATUS_OK)
        return status;

    for (y = 0; y < tile_height; ++y)
    {
        memcpy(
            tile->data + (size_t)y * (size_t)tile_width * (size_t)image->channels,
            image->data + ((size_t)(tile_y + y) * (size_t)image->width + (size_t)tile_x) * (size_t)image->channels,
            (size_t)tile_width * (size_t)image->channels
        );
    }
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.5.1, every tile in the grid contributes at least one tile-part. */
static dic_status j2k_image_alloc_tile_payloads(
    size_t tile_count,
    j2k_image_tile_payloads *tile_payloads
)
{
    j2k_DEBUG_ENTER();
    size_t index;

    if (tile_payloads == NULL || tile_count == 0u || tile_count > UINT16_MAX + 1u)
        return DIC_STATUS_INVALID_ARGUMENT;

    j2k_image_tile_payloads_free(tile_payloads);
    tile_payloads->tile_parts = (j2k_tile_part_payload *)calloc(tile_count, sizeof(tile_payloads->tile_parts[0]));
    tile_payloads->payloads = (j2k_image_payload *)calloc(tile_count, sizeof(tile_payloads->payloads[0]));
    if (tile_payloads->tile_parts == NULL || tile_payloads->payloads == NULL)
    {
        j2k_image_tile_payloads_free(tile_payloads);
        return DIC_STATUS_MEMORY_ERROR;
    }

    tile_payloads->count = tile_count;
    for (index = 0u; index < tile_count; ++index)
        j2k_image_payload_init(tile_payloads->payloads + index);
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex F.4 and Annex B.10, transformed tile-components are entropy coded into packetized code-block contributions. */
static dic_status j2k_image_encode_payload(
    const dic_image_u8 *image,
    int requested_levels,
    uint16_t layers,
    const dic_rect_i32 *roi_rect,
    uint8_t roi_shift,
    int quality,
    j2k_basic_params *params,
    j2k_image_payload *payload
)
{
    j2k_DEBUG_ENTER();
    int32_t *planes = NULL;
    uint8_t *roi_shift_map = NULL;
    dic_status status;
    int levels;
    int component;
    size_t plane_samples;

    if (image == NULL || params == NULL || payload == NULL || requested_levels < 0 || layers == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (quality != -1)
    {
        if (roi_shift > 0u || roi_rect != NULL)
            return DIC_STATUS_INVALID_ARGUMENT;
        return j2k_image_encode_lossy_payload(image, requested_levels, layers, quality, params, payload);
    }
    if (roi_shift > 0u && roi_rect == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (image->width <= 0 || image->height <= 0 || (image->channels != 1 && image->channels != 3))
        return DIC_STATUS_INVALID_ARGUMENT;

    levels = j2k_image_effective_levels(image->width, image->height, requested_levels);
    status = j2k_image_make_planes(image, &planes);
    if (status != DIC_STATUS_OK)
        return status;
    if (roi_shift > 0u)
    {
        status = j2k_roi_build_shift_map(
            image->width,
            image->height,
            levels,
            roi_rect,
            &roi_shift_map
        );
    }

    plane_samples = (size_t)image->width * (size_t)image->height;
    for (component = 0; status == DIC_STATUS_OK && component < image->channels; ++component)
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
        status = j2k_roi_apply_shift_map(
            planes + (size_t)component * plane_samples,
            image->width,
            image->height,
            roi_shift_map,
            roi_shift
        );
    }

    if (status == DIC_STATUS_OK)
        status = j2k_image_build_payload(
            planes,
            image->width,
            image->height,
            image->channels,
            levels,
            layers,
            roi_shift,
            NULL,
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
        params->layers = layers;
        params->roi_shift = roi_shift;
        params->use_sop = 1u;
        params->use_eph = 1u;
        j2k_image_set_max_precincts(params);
    }

    free(roi_shift_map);
    free(planes);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.5.1, main-header SIZ describes the full reference grid and regular tile size. */
static void j2k_image_set_main_params(
    const dic_image_u8 *image,
    int levels,
    int tile_width,
    int tile_height,
    uint16_t layers,
    j2k_basic_params *params
)
{
    j2k_DEBUG_ENTER();

    params->width = (uint32_t)image->width;
    params->height = (uint32_t)image->height;
    params->components = (uint16_t)image->channels;
    params->decomposition_levels = (uint8_t)levels;
    params->reversible = 1u;
    params->multiple_component_transform = image->channels == 3 ? 1u : 0u;
    params->layers = layers;
    params->tile_width = tile_width == image->width ? 0u : (uint32_t)tile_width;
    params->tile_height = tile_height == image->height ? 0u : (uint32_t)tile_height;
    params->use_sop = 1u;
    params->use_eph = 1u;
    j2k_image_set_max_precincts(params);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.5-B.10, each tile is transformed and packetized independently. */
static dic_status j2k_image_encode_tile_parts(
    const dic_image_u8 *image,
    int requested_levels,
    int tile_width,
    int tile_height,
    uint16_t layers,
    j2k_basic_params *params,
    j2k_image_tile_payloads *tile_payloads
)
{
    j2k_DEBUG_ENTER();
    int tiles_x;
    int tiles_y;
    int tile_index = 0;
    int levels;
    int ty;
    dic_status status;

    if (image == NULL || params == NULL || tile_payloads == NULL || requested_levels < 0 || layers == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (image->width <= 0 || image->height <= 0 || (image->channels != 1 && image->channels != 3))
        return DIC_STATUS_INVALID_ARGUMENT;
    if (tile_width <= 0 || tile_height <= 0 || tile_width > image->width || tile_height > image->height)
        return DIC_STATUS_INVALID_ARGUMENT;

    tiles_x = (image->width + tile_width - 1) / tile_width;
    tiles_y = (image->height + tile_height - 1) / tile_height;
    if ((size_t)tiles_x > (size_t)-1 / (size_t)tiles_y || (size_t)tiles_x * (size_t)tiles_y > UINT16_MAX + 1u)
        return DIC_STATUS_INVALID_ARGUMENT;

    levels = requested_levels;
    for (ty = 0; ty < tiles_y; ++ty)
    {
        int tx;

        for (tx = 0; tx < tiles_x; ++tx)
        {
            int current_width = tx + 1 == tiles_x ? image->width - tx * tile_width : tile_width;
            int current_height = ty + 1 == tiles_y ? image->height - ty * tile_height : tile_height;
            int effective = j2k_image_effective_levels(current_width, current_height, requested_levels);

            if (effective < levels)
                levels = effective;
        }
    }

    status = j2k_image_alloc_tile_payloads((size_t)tiles_x * (size_t)tiles_y, tile_payloads);
    if (status != DIC_STATUS_OK)
        return status;

    for (ty = 0; ty < tiles_y && status == DIC_STATUS_OK; ++ty)
    {
        int tx;

        for (tx = 0; tx < tiles_x && status == DIC_STATUS_OK; ++tx)
        {
            int current_width = tx + 1 == tiles_x ? image->width - tx * tile_width : tile_width;
            int current_height = ty + 1 == tiles_y ? image->height - ty * tile_height : tile_height;
            dic_image_u8 tile;
            j2k_basic_params tile_params;

            dic_image_u8_init(&tile);
            memset(&tile_params, 0, sizeof(tile_params));
            status = j2k_image_copy_tile(
                image,
                tx * tile_width,
                ty * tile_height,
                current_width,
                current_height,
                &tile
            );
            if (status == DIC_STATUS_OK)
            {
                status = j2k_image_encode_payload(
                    &tile,
                    levels,
                    layers,
                    NULL,
                    0u,
                    -1,
                    &tile_params,
                    tile_payloads->payloads + tile_index
                );
            }
            if (status == DIC_STATUS_OK)
            {
                tile_payloads->tile_parts[tile_index].tile_index = (uint16_t)tile_index;
                tile_payloads->tile_parts[tile_index].tile_part_index = 0u;
                tile_payloads->tile_parts[tile_index].tile_part_count = 1u;
                tile_payloads->tile_parts[tile_index].payload = tile_payloads->payloads[tile_index].data;
                tile_payloads->tile_parts[tile_index].payload_size = tile_payloads->payloads[tile_index].size;
            }
            dic_image_u8_free(&tile);
            ++tile_index;
        }
    }

    if (status == DIC_STATUS_OK)
        j2k_image_set_main_params(image, levels, tile_width, tile_height, layers, params);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A.3-A.4, a raw codestream is main header, one tile-part SOD payload, and EOC. */
dic_status j2k_write_image_codestream(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int quality
)
{
    j2k_DEBUG_ENTER();
    j2k_basic_params params = {0};
    j2k_image_payload payload;
    dic_status status;

    j2k_image_payload_init(&payload);
    if (path == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = j2k_image_encode_payload(image, requested_levels, 1u, NULL, 0u, quality, &params, &payload);
    if (status == DIC_STATUS_OK)
        status = j2k_write_codestream_with_payload(path, &params, payload.data, payload.size);

    j2k_image_payload_free(&payload);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex H.2-H.3, ROI Maxshift applies a wavelet-domain coefficient mask before EBCOT coding. */
dic_status j2k_write_image_codestream_roi(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    const dic_rect_i32 *roi_rect,
    uint8_t roi_shift
)
{
    j2k_DEBUG_ENTER();
    j2k_basic_params params = {0};
    j2k_image_payload payload;
    dic_status status;

    j2k_image_payload_init(&payload);
    if (path == NULL || roi_rect == NULL || roi_shift == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = j2k_image_encode_payload(image, requested_levels, 1u, roi_rect, roi_shift, -1, &params, &payload);
    if (status == DIC_STATUS_OK)
        status = j2k_write_codestream_with_payload(path, &params, payload.data, payload.size);

    j2k_image_payload_free(&payload);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.5, a tiled codestream writes one SOT/SOD tile-part per tile. */
dic_status j2k_write_image_codestream_tiled(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int tile_width,
    int tile_height,
    uint16_t layers
)
{
    j2k_DEBUG_ENTER();
    j2k_basic_params params = {0};
    j2k_image_tile_payloads tile_payloads;
    dic_status status;

    j2k_image_tile_payloads_init(&tile_payloads);
    if (path == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = j2k_image_encode_tile_parts(
        image,
        requested_levels,
        tile_width,
        tile_height,
        layers,
        &params,
        &tile_payloads
    );
    if (status == DIC_STATUS_OK)
    {
        status = j2k_write_codestream_with_tile_parts(
            path,
            &params,
            tile_payloads.tile_parts,
            tile_payloads.count
        );
    }

    j2k_image_tile_payloads_free(&tile_payloads);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1, JP2 stores the same codestream in a Contiguous Codestream box. */
dic_status j2k_write_image_jp2(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int quality
)
{
    j2k_DEBUG_ENTER();
    j2k_basic_params params = {0};
    j2k_image_payload payload;
    dic_status status;

    j2k_image_payload_init(&payload);
    if (path == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = j2k_image_encode_payload(image, requested_levels, 1u, NULL, 0u, quality, &params, &payload);
    if (status == DIC_STATUS_OK)
        status = jp2_write_file_with_codestream_payload(path, &params, payload.data, payload.size);

    j2k_image_payload_free(&payload);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1 and Annex H, JP2 wraps the ROI-shifted codestream and RGN marker. */
dic_status j2k_write_image_jp2_roi(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    const dic_rect_i32 *roi_rect,
    uint8_t roi_shift
)
{
    j2k_DEBUG_ENTER();
    j2k_basic_params params = {0};
    j2k_image_payload payload;
    dic_status status;

    j2k_image_payload_init(&payload);
    if (path == NULL || roi_rect == NULL || roi_shift == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = j2k_image_encode_payload(image, requested_levels, 1u, roi_rect, roi_shift, -1, &params, &payload);
    if (status == DIC_STATUS_OK)
        status = jp2_write_file_with_codestream_payload(path, &params, payload.data, payload.size);

    j2k_image_payload_free(&payload);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1, JP2 stores the complete multi-tile codestream in one jp2c box. */
dic_status j2k_write_image_jp2_tiled(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int tile_width,
    int tile_height,
    uint16_t layers
)
{
    j2k_DEBUG_ENTER();
    j2k_basic_params params = {0};
    j2k_image_tile_payloads tile_payloads;
    dic_status status;

    j2k_image_tile_payloads_init(&tile_payloads);
    if (path == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = j2k_image_encode_tile_parts(
        image,
        requested_levels,
        tile_width,
        tile_height,
        layers,
        &params,
        &tile_payloads
    );
    if (status == DIC_STATUS_OK)
    {
        status = jp2_write_file_with_codestream_tile_parts(
            path,
            &params,
            tile_payloads.tile_parts,
            tile_payloads.count
        );
    }

    j2k_image_tile_payloads_free(&tile_payloads);
    return status;
}

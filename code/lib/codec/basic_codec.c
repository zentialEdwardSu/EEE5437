/**
 * @file basic_codec.c
 * @brief Implements the progressive bitplane 5/3-DWT image codec pipeline.
 */

#include "codec/basic_codec.h"

#include <stdlib.h>
#include <string.h>

#include "codec/predict.h"
#include "codec/quant.h"
#include "codec/subband.h"
#include "wavelet/dic_dwt53.h"

void codec_basic_encoded_init(codec_basic_encoded_image *encoded)
{
    if (encoded == NULL) return;
    encoded->width = 0;
    encoded->height = 0;
    encoded->channels = 0;
    encoded->levels = 0;
    encoded->quant_step = 0;
    encoded->channel_streams = NULL;
}

void codec_basic_encoded_free(codec_basic_encoded_image *encoded)
{
    int channel;
    if (encoded == NULL) return;
    if (encoded->channel_streams != NULL) {
        for (channel = 0; channel < encoded->channels; ++channel) {
            codec_basic_channel_stream *stream = encoded->channel_streams + channel;
            int res;
            if (stream->resolutions != NULL) {
                for (res = 0; res < stream->num_resolutions; ++res) {
                    codec_basic_resolution_stream *rs = stream->resolutions + res;
                    int bp;
                    if (rs->bitplanes != NULL) {
                        for (bp = 0; bp < rs->num_bitplanes; ++bp)
                            codec_scan_bitplane_free(rs->bitplanes + bp);
                        free(rs->bitplanes);
                    }
                }
                free(stream->resolutions);
            }
        }
        free(encoded->channel_streams);
    }
    codec_basic_encoded_init(encoded);
}

static dic_status codec_basic_validate_params(
    int width, int height, int channels, int levels, int quant_step)
{
    if (channels != 1 && channels != 3)
        return DIC_HW4_INVALID_CHANNELS;
    if (quant_step <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    return dic_dwt53_validate_levels(width, height, levels);
}

static void codec_basic_copy_channel_to_plane(
    const uint8_t *input, int width, int height, int channels, int channel, int32_t *plane)
{
    int y;
    for (y = 0; y < height; ++y) {
        int x;
        for (x = 0; x < width; ++x) {
            size_t pixel = ((size_t)y * (size_t)width) + (size_t)x;
            plane[pixel] = (int32_t)input[(pixel * (size_t)channels) + (size_t)channel];
        }
    }
}

static uint8_t codec_basic_clamp_u8(int32_t value)
{
    if (value < 0) return 0u;
    if (value > 255) return 255u;
    return (uint8_t)value;
}

static void codec_basic_copy_plane_to_channel(
    const int32_t *plane, int width, int height, int channels, int channel, uint8_t *output)
{
    int y;
    for (y = 0; y < height; ++y) {
        int x;
        for (x = 0; x < width; ++x) {
            size_t pixel = ((size_t)y * (size_t)width) + (size_t)x;
            output[(pixel * (size_t)channels) + (size_t)channel] = codec_basic_clamp_u8(plane[pixel]);
        }
    }
}

dic_status codec_basic_encode_image(
    const uint8_t *input, int width, int height, int channels,
    int levels, int quant_step, codec_basic_encoded_image *encoded)
{
    size_t plane_count;
    int32_t *plane = NULL;
    dic_rect_i32 ll_rect;
    dic_status status;
    int channel;
    int resolution;

    if (input == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = codec_basic_validate_params(width, height, channels, levels, quant_step);
    if (status != DIC_STATUS_OK) return status;

    plane_count = (size_t)width * (size_t)height;
    plane = (int32_t *)malloc(plane_count * sizeof(plane[0]));
    if (plane == NULL) return DIC_STATUS_MEMORY_ERROR;

    codec_basic_encoded_free(encoded);

    encoded->channel_streams = (codec_basic_channel_stream *)calloc(
        (size_t)channels, sizeof(encoded->channel_streams[0]));
    if (encoded->channel_streams == NULL) { free(plane); return DIC_STATUS_MEMORY_ERROR; }

    encoded->width = width;
    encoded->height = height;
    encoded->channels = channels;
    encoded->levels = levels;
    encoded->quant_step = quant_step;

    status = codec_subband_lowest_ll_rect(width, height, levels, &ll_rect);
    if (status != DIC_STATUS_OK) { free(plane); codec_basic_encoded_free(encoded); return status; }

    for (channel = 0; channel < channels; ++channel) {
        codec_basic_channel_stream *stream = encoded->channel_streams + channel;

        stream->num_resolutions = levels + 1;
        stream->resolutions = (codec_basic_resolution_stream *)calloc(
            (size_t)stream->num_resolutions, sizeof(stream->resolutions[0]));
        if (stream->resolutions == NULL) {
            free(plane);
            codec_basic_encoded_free(encoded);
            return DIC_STATUS_MEMORY_ERROR;
        }

        codec_basic_copy_channel_to_plane(input, width, height, channels, channel, plane);

        status = dic_dwt53_forward_plane(plane, width, height, levels);
        if (status == DIC_STATUS_OK)
            status = codec_quant_scalar_i32(plane, plane_count, quant_step);
        if (status == DIC_STATUS_OK)
            status = codec_predict_ll_left(plane, width, ll_rect);

        /* Encode each resolution independently */
        for (resolution = 0; status == DIC_STATUS_OK && resolution <= levels; ++resolution) {
            codec_basic_resolution_stream *rs = stream->resolutions + resolution;

            rs->resolution = resolution;
            status = codec_scan_encode_subbands(
                plane, width, height, levels, resolution,
                &rs->bitplanes, &rs->num_bitplanes);
        }

        if (status != DIC_STATUS_OK) {
            free(plane);
            codec_basic_encoded_free(encoded);
            return status;
        }
    }

    free(plane);
    return DIC_STATUS_OK;
}

/** Compute output image size for max_resolution levels of IDWT. */
static void codec_basic_output_size(
    int width, int height, int levels, int max_resolution,
    int *out_width, int *out_height)
{
    int i;
    int steps = levels - max_resolution;

    *out_width = width;
    *out_height = height;
    for (i = 0; i < steps; ++i) {
        *out_width = dic_dwt53_low_size(*out_width);
        *out_height = dic_dwt53_low_size(*out_height);
    }
}

dic_status codec_basic_decode_image(
    const codec_basic_encoded_image *encoded,
    int max_resolution,
    int num_bitplanes,
    dic_image_u8 *decoded)
{
    int out_width, out_height;
    size_t plane_count;
    int32_t *plane = NULL;
    dic_rect_i32 ll_rect;
    dic_status status;
    int channel;
    int resolution;

    if (encoded == NULL || decoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (max_resolution < 0 || max_resolution > encoded->levels)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (num_bitplanes < 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = codec_basic_validate_params(
        encoded->width, encoded->height, encoded->channels,
        encoded->levels, encoded->quant_step);
    if (status != DIC_STATUS_OK) return status;

    codec_basic_output_size(encoded->width, encoded->height, encoded->levels,
                            max_resolution, &out_width, &out_height);

    plane_count = (size_t)out_width * (size_t)out_height;
    plane = (int32_t *)calloc(plane_count, sizeof(plane[0]));
    if (plane == NULL) return DIC_STATUS_MEMORY_ERROR;

    status = dic_image_u8_alloc(decoded, out_width, out_height, encoded->channels);
    if (status != DIC_STATUS_OK) { free(plane); return status; }

    /* LL rect in output plane (used for inverse LL prediction) */
    if (max_resolution > 0)
    {
        status = codec_subband_lowest_ll_rect(out_width, out_height, max_resolution, &ll_rect);
    }
    else
    {
        ll_rect.x = 0;
        ll_rect.y = 0;
        ll_rect.width = out_width;
        ll_rect.height = out_height;
    }
    if (status != DIC_STATUS_OK) { dic_image_u8_free(decoded); free(plane); return status; }

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        const codec_basic_channel_stream *stream = encoded->channel_streams + channel;

        /* Zero the plane so positions not touched by this channel's
           scan decode (e.g. ZTR-pruned subtrees) start clean. */
        memset(plane, 0, plane_count * sizeof(plane[0]));

        /* Decode each resolution into the output plane */
        for (resolution = 0; resolution <= max_resolution; ++resolution)
        {
            const codec_basic_resolution_stream *rs = stream->resolutions + resolution;
            int bp_to_decode;

            if (rs->num_bitplanes == 0)
                continue;  /* All-zero resolution — plane is already zeroed */

            bp_to_decode = (num_bitplanes == 0 || num_bitplanes > rs->num_bitplanes)
                ? rs->num_bitplanes : num_bitplanes;

            status = codec_scan_decode_subbands(
                rs->bitplanes, rs->num_bitplanes, bp_to_decode,
                out_width, out_height, max_resolution, resolution, plane);
            if (status != DIC_STATUS_OK) break;
        }
        if (status != DIC_STATUS_OK) break;

        /* Inverse LL prediction (on LL region of output plane) */
        if (max_resolution > 0 || encoded->levels > 0)
            status = codec_unpredict_ll_left(plane, out_width, ll_rect);
        if (status != DIC_STATUS_OK) break;

        /* Dequantize */
        status = codec_dequant_scalar_i32(plane, plane_count, encoded->quant_step);
        if (status != DIC_STATUS_OK) break;

        /* Inverse DWT — only max_resolution levels */
        if (max_resolution > 0)
            status = dic_dwt53_inverse_plane(plane, out_width, out_height, max_resolution);
        if (status != DIC_STATUS_OK) break;

        /* Copy decoded plane to interleaved output */
        codec_basic_copy_plane_to_channel(
            plane, out_width, out_height, encoded->channels, channel, decoded->data);
    }

    free(plane);
    if (status != DIC_STATUS_OK)
    {
        dic_image_u8_free(decoded);
        return status;
    }
    return DIC_STATUS_OK;
}

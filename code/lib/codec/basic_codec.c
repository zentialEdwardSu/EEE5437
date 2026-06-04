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
    encoded->num_bitplanes = 0;
    encoded->channel_streams = NULL;
}

void codec_basic_encoded_free(codec_basic_encoded_image *encoded)
{
    int channel;
    if (encoded == NULL) return;
    if (encoded->channel_streams != NULL) {
        for (channel = 0; channel < encoded->channels; ++channel) {
            codec_basic_channel_stream *stream = encoded->channel_streams + channel;
            int bp;
            if (stream->bitplanes != NULL) {
                for (bp = 0; bp < stream->num_bitplanes; ++bp)
                    codec_scan_bitplane_free(stream->bitplanes + bp);
                free(stream->bitplanes);
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
        codec_scan_bitplane *bps = NULL;
        int bp_count = 0;

        codec_basic_copy_channel_to_plane(input, width, height, channels, channel, plane);

        status = dic_dwt53_forward_plane(plane, width, height, levels);
        if (status == DIC_STATUS_OK)
            status = codec_quant_scalar_i32(plane, plane_count, quant_step);
        if (status == DIC_STATUS_OK)
            status = codec_predict_ll_left(plane, width, ll_rect);
        if (status == DIC_STATUS_OK)
            status = codec_scan_encode_plane(plane, width, height, levels, &bps, &bp_count);

        if (status != DIC_STATUS_OK) {
            free(plane);
            codec_basic_encoded_free(encoded);
            return status;
        }

        stream->bitplanes = bps;
        stream->num_bitplanes = bp_count;
        encoded->num_bitplanes = bp_count;
    }

    free(plane);
    return DIC_STATUS_OK;
}

dic_status codec_basic_decode_image(
    const codec_basic_encoded_image *encoded, dic_image_u8 *decoded)
{
    if (encoded == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    return codec_basic_decode_image_bitplanes(encoded, encoded->num_bitplanes, decoded);
}

dic_status codec_basic_decode_image_bitplanes(
    const codec_basic_encoded_image *encoded, int num_bitplanes, dic_image_u8 *decoded)
{
    size_t plane_count;
    int32_t *plane = NULL;
    dic_rect_i32 ll_rect;
    dic_status status;
    int channel;

    if (encoded == NULL || decoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (num_bitplanes <= 0 || num_bitplanes > encoded->num_bitplanes)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = codec_basic_validate_params(
        encoded->width, encoded->height, encoded->channels,
        encoded->levels, encoded->quant_step);
    if (status != DIC_STATUS_OK) return status;

    plane_count = (size_t)encoded->width * (size_t)encoded->height;
    plane = (int32_t *)malloc(plane_count * sizeof(plane[0]));
    if (plane == NULL) return DIC_STATUS_MEMORY_ERROR;

    status = dic_image_u8_alloc(decoded, encoded->width, encoded->height, encoded->channels);
    if (status != DIC_STATUS_OK) { free(plane); return status; }

    status = codec_subband_lowest_ll_rect(
        encoded->width, encoded->height, encoded->levels, &ll_rect);
    if (status != DIC_STATUS_OK) { dic_image_u8_free(decoded); free(plane); return status; }

    for (channel = 0; channel < encoded->channels; ++channel) {
        const codec_basic_channel_stream *stream = encoded->channel_streams + channel;

        status = codec_scan_decode_plane(
            stream->bitplanes, stream->num_bitplanes, num_bitplanes,
            encoded->width, encoded->height, encoded->levels, plane);
        if (status == DIC_STATUS_OK)
            status = codec_unpredict_ll_left(plane, encoded->width, ll_rect);
        if (status == DIC_STATUS_OK)
            status = codec_dequant_scalar_i32(plane, plane_count, encoded->quant_step);
        if (status == DIC_STATUS_OK)
            status = dic_dwt53_inverse_plane(plane, encoded->width, encoded->height, encoded->levels);

        if (status != DIC_STATUS_OK) {
            dic_image_u8_free(decoded);
            free(plane);
            return status;
        }

        codec_basic_copy_plane_to_channel(
            plane, encoded->width, encoded->height, encoded->channels, channel, decoded->data);
    }

    free(plane);
    return DIC_STATUS_OK;
}

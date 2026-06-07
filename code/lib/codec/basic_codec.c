/**
 * @file basic_codec.c
 * @brief Full-plane progressive 5/3-DWT codec implementation.
 *
 * Encoding and decoding are deliberately symmetric:
 *
 * @code{.unparsed}
 * ENCODE                                  DECODE
 * ------                                  ------
 * interleaved u8                          bit-plane prefix
 *   -> component planes                     -> coefficient planes
 *   -> RGB RCT                              -> LL inverse prediction
 *   -> forward 5/3 DWT                      -> scalar dequantization
 *   -> scalar quantization                  -> inverse 5/3 DWT
 *   -> LL left prediction                   -> inverse RGB RCT
 *   -> progressive scan                     -> interleaved/clamped u8
 * @endcode
 */

#include "codec/basic_codec.h"

#include <math.h>
#include <stdlib.h>

#include "codec/predict.h"
#include "codec/quant.h"
#include "codec/rct.h"
#include "codec/subband.h"
#include "wavelet/dic_dwt53.h"

/** @brief Establishes the empty-state invariant used by all cleanup paths. */
void codec_basic_encoded_init(codec_basic_encoded_image* encoded) {
    if (encoded == NULL) return;
    encoded->width = 0;
    encoded->height = 0;
    encoded->channels = 0;
    encoded->levels = 0;
    encoded->quant_step = 0.0f;
    encoded->channel_streams = NULL;
}

/** @brief Recursively releases channel arrays and their bit-plane payloads. */
void codec_basic_encoded_free(codec_basic_encoded_image* encoded) {
    int channel;
    if (encoded == NULL) return;
    if (encoded->channel_streams != NULL) {
        for (channel = 0; channel < encoded->channels; ++channel) {
            codec_basic_channel_stream* stream =
                encoded->channel_streams + channel;
            int bp;
            for (bp = 0; bp < stream->num_bitplanes; ++bp)
                codec_scan_bitplane_free(stream->bitplanes + bp);
            free(stream->bitplanes);
        }
        free(encoded->channel_streams);
    }
    codec_basic_encoded_init(encoded);
}

/** @brief Validates dimensions, component count, levels, and quantization. */
static dic_status codec_basic_validate_params(int width, int height,
                                              int channels, int levels,
                                              float quant_step) {
    if (channels != 1 && channels != 3) return DIC_HW4_INVALID_CHANNELS;
    if (!isfinite(quant_step) || quant_step <= 0.0f)
        return DIC_STATUS_INVALID_ARGUMENT;
    return dic_dwt53_validate_levels(width, height, levels);
}

/**
 * @brief Allocates only channel descriptors; scanners allocate their layers.
 */
dic_status codec_basic_encoded_alloc_streams(codec_basic_encoded_image* encoded,
                                             int width, int height,
                                             int channels, int levels,
                                             float quant_step) {
    dic_status status;
    if (encoded == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    status =
        codec_basic_validate_params(width, height, channels, levels, quant_step);
    if (status != DIC_STATUS_OK) return status;

    codec_basic_encoded_free(encoded);
    encoded->channel_streams = (codec_basic_channel_stream*)calloc(
        (size_t)channels, sizeof(encoded->channel_streams[0]));
    if (encoded->channel_streams == NULL) return DIC_STATUS_MEMORY_ERROR;
    encoded->width = width;
    encoded->height = height;
    encoded->channels = channels;
    encoded->levels = levels;
    encoded->quant_step = quant_step;
    return DIC_STATUS_OK;
}

/** @brief Deinterleaves one component into a signed working plane. */
static void codec_basic_copy_channel_to_plane(const uint8_t* input, int width,
                                              int height, int channels,
                                              int channel, int32_t* plane) {
    size_t count = (size_t)width * (size_t)height;
    size_t pixel;
    for (pixel = 0u; pixel < count; ++pixel)
        plane[pixel] =
            (int32_t)input[pixel * (size_t)channels + (size_t)channel];
}

/** @brief Saturates a reconstructed signed sample to the output u8 range. */
static uint8_t codec_basic_clamp_u8(int32_t value) {
    if (value < 0) return 0u;
    if (value > 255) return 255u;
    return (uint8_t)value;
}

/** @brief Interleaves one reconstructed plane into the output image. */
static void codec_basic_copy_plane_to_channel(const int32_t* plane, int width,
                                              int height, int channels,
                                              int channel, uint8_t* output) {
    size_t count = (size_t)width * (size_t)height;
    size_t pixel;
    for (pixel = 0u; pixel < count; ++pixel)
        output[pixel * (size_t)channels + (size_t)channel] =
            codec_basic_clamp_u8(plane[pixel]);
}

/**
 * @brief Runs the complete forward pipeline and creates channel streams.
 *
 * The scanner receives the full packed DWT plane, not one subband at a time.
 * This is what permits zerotree relationships across resolutions.
 */
dic_status codec_basic_encode_image(const uint8_t* input, int width, int height,
                                    int channels, int levels, float quant_step,
                                    codec_basic_encoded_image* encoded) {
    int32_t* planes[3] = {NULL, NULL, NULL};
    size_t plane_count;
    dic_rect_i32 ll_rect;
    dic_status status;
    int channel;

    if (input == NULL || encoded == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    status =
        codec_basic_validate_params(width, height, channels, levels, quant_step);
    if (status != DIC_STATUS_OK) return status;
    status = codec_subband_lowest_ll_rect(width, height, levels, &ll_rect);
    if (status != DIC_STATUS_OK) return status;
    status = codec_basic_encoded_alloc_streams(encoded, width, height, channels,
                                               levels, quant_step);
    if (status != DIC_STATUS_OK) return status;

    plane_count = (size_t)width * (size_t)height;
    for (channel = 0; channel < channels; ++channel) {
        planes[channel] =
            (int32_t*)malloc(plane_count * sizeof(planes[channel][0]));
        if (planes[channel] == NULL) {
            status = DIC_STATUS_MEMORY_ERROR;
            goto cleanup;
        }
        codec_basic_copy_channel_to_plane(input, width, height, channels,
                                          channel, planes[channel]);
    }

    if (channels == 3)
        status = codec_rct_forward(planes[0], planes[1], planes[2],
                                   plane_count);

    for (channel = 0; status == DIC_STATUS_OK && channel < channels;
         ++channel) {
        codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        status =
            dic_dwt53_forward_plane(planes[channel], width, height, levels);
        if (status == DIC_STATUS_OK)
            status =
                codec_quant_scalar_i32(planes[channel], plane_count, quant_step);
        if (status == DIC_STATUS_OK)
            status = codec_predict_ll_left(planes[channel], width, ll_rect);
        if (status == DIC_STATUS_OK)
            status = codec_scan_encode_plane(
                planes[channel], width, height, levels, &stream->bitplanes,
                &stream->num_bitplanes);
    }

cleanup:
    for (channel = 0; channel < 3; ++channel) free(planes[channel]);
    if (status != DIC_STATUS_OK) codec_basic_encoded_free(encoded);
    return status;
}

/**
 * @brief Runs the inverse pipeline for an MSB-first quality prefix.
 *
 * Each channel may own a different total layer count. @p num_bitplanes is
 * independently clamped per channel before coefficient reconstruction.
 */
dic_status codec_basic_decode_image(const codec_basic_encoded_image* encoded,
                                    int num_bitplanes,
                                    dic_image_u8* decoded) {
    int32_t* planes[3] = {NULL, NULL, NULL};
    size_t plane_count;
    dic_rect_i32 ll_rect;
    dic_status status;
    int channel;

    if (encoded == NULL || decoded == NULL ||
        encoded->channel_streams == NULL || num_bitplanes < 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    status = codec_basic_validate_params(encoded->width, encoded->height,
                                         encoded->channels, encoded->levels,
                                         encoded->quant_step);
    if (status != DIC_STATUS_OK) return status;
    status = codec_subband_lowest_ll_rect(encoded->width, encoded->height,
                                          encoded->levels, &ll_rect);
    if (status != DIC_STATUS_OK) return status;
    status = dic_image_u8_alloc(decoded, encoded->width, encoded->height,
                                encoded->channels);
    if (status != DIC_STATUS_OK) return status;

    plane_count = (size_t)encoded->width * (size_t)encoded->height;
    for (channel = 0; channel < encoded->channels; ++channel) {
        const codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        int decode_count =
            num_bitplanes == 0 || num_bitplanes > stream->num_bitplanes
                ? stream->num_bitplanes
                : num_bitplanes;
        planes[channel] =
            (int32_t*)calloc(plane_count, sizeof(planes[channel][0]));
        if (planes[channel] == NULL) {
            status = DIC_STATUS_MEMORY_ERROR;
            goto cleanup;
        }
        if (decode_count > 0)
            status = codec_scan_decode_plane(
                stream->bitplanes, stream->num_bitplanes, decode_count,
                encoded->width, encoded->height, encoded->levels,
                planes[channel]);
        if (status == DIC_STATUS_OK)
            status = codec_unpredict_ll_left(planes[channel], encoded->width,
                                             ll_rect);
        if (status == DIC_STATUS_OK)
            status = codec_dequant_scalar_i32(
                planes[channel], plane_count, encoded->quant_step);
        if (status == DIC_STATUS_OK)
            status = dic_dwt53_inverse_plane(
                planes[channel], encoded->width, encoded->height,
                encoded->levels);
        if (status != DIC_STATUS_OK) goto cleanup;
    }

    if (encoded->channels == 3)
        status = codec_rct_inverse(planes[0], planes[1], planes[2],
                                   plane_count);
    for (channel = 0; status == DIC_STATUS_OK &&
                      channel < encoded->channels;
         ++channel)
        codec_basic_copy_plane_to_channel(
            planes[channel], encoded->width, encoded->height,
            encoded->channels, channel, decoded->data);

cleanup:
    for (channel = 0; channel < 3; ++channel) free(planes[channel]);
    if (status != DIC_STATUS_OK) dic_image_u8_free(decoded);
    return status;
}

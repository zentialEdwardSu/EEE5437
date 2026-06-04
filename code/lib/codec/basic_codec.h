#pragma once
/**
 * @file basic_codec.h
 * @brief Basic image codec pipeline with progressive bitplane coding over 5/3 DWT coefficients.
 *
 * Encoding: interleaved 8-bit samples → deinterleave → 5/3 DWT → scalar quantize
 * → LL predict → EZW per-bitplane scan (MSB→LSB, auto-detected count).
 * Decoding: inverse operations with progressive quality via decode_image_bitplanes().
 */

#include <stddef.h>
#include <stdint.h>

#include "codec/scan.h"
#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Encoded data for one image channel. */
typedef struct codec_basic_channel_stream
{
    /** Bitplanes for this channel (MSB-first). */
    codec_scan_bitplane *bitplanes;
    /** Number of bitplanes (auto-detected during encoding). */
    int num_bitplanes;
} codec_basic_channel_stream;

/** Encoded representation of a full grayscale or RGB image with progressive bitplanes. */
typedef struct codec_basic_encoded_image
{
    int width;
    int height;
    int channels;
    int levels;
    int quant_step;
    /** Total number of bitplanes (max_bp + 1, auto-detected). */
    int num_bitplanes;
    /** One channel stream per channel. */
    codec_basic_channel_stream *channel_streams;
} codec_basic_encoded_image;

void codec_basic_encoded_init(codec_basic_encoded_image *encoded);
void codec_basic_encoded_free(codec_basic_encoded_image *encoded);

/**
 * @brief Encodes an interleaved 8-bit image with progressive bitplane coding.
 *
 * The number of bitplanes is auto-detected from the maximum coefficient
 * magnitude after quantization.  Each bitplane stores a Huffman-coded
 * significance pass (with EZT) and raw refinement bits.
 *
 * @param input      Interleaved source samples in row-major order.
 * @param width      Source width in pixels.
 * @param height     Source height in pixels.
 * @param channels   Source channel count; must be 1 or 3.
 * @param levels     Number of 5/3 DWT decomposition levels.
 * @param quant_step Positive scalar quantization step.
 * @param encoded    Output encoded image. Existing contents are freed.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int levels,
    int quant_step,
    codec_basic_encoded_image *encoded);

/**
 * @brief Decodes all bitplanes (full quality).
 * @param encoded Encoded image.
 * @param decoded Output image; receives allocated sample storage on success.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_decode_image(
    const codec_basic_encoded_image *encoded,
    dic_image_u8 *decoded);

/**
 * @brief Decodes only the first num_bitplanes for progressive quality refinement.
 *
 * When num_bitplanes < encoded->num_bitplanes, midpoint reconstruction is
 * applied for the first missing bitplane.  Fewer bitplanes = faster decode
 * with lower quality.
 *
 * @param encoded       Encoded image.
 * @param num_bitplanes Number of bitplanes to decode (1..total).
 * @param decoded       Output image.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_decode_image_bitplanes(
    const codec_basic_encoded_image *encoded,
    int num_bitplanes,
    dic_image_u8 *decoded);

#ifdef __cplusplus
}
#endif

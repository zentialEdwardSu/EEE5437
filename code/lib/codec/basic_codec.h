#pragma once
/**
 * @file basic_codec.h
 * @brief Basic image codec pipeline with resolution and quality scalability.
 *
 * Encoding: interleaved 8-bit samples → deinterleave → 5/3 DWT → scalar
 * quantize → LL predict → per-resolution EZW bitplane scan (MSB→LSB, no
 * zerotree).
 *
 * Decoding: per-resolution EZW decode → inverse LL predict → dequantize →
 * max_resolution levels of inverse DWT.
 *
 * Scalability dimensions:
 *   - max_resolution: 0 = LL only, levels = full resolution
 *   - num_bitplanes:  1..N for progressive quality, 0 = all
 */

#include <stddef.h>
#include <stdint.h>

#include "codec/scan.h"
#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Encoded data for one resolution level of one image channel. */
typedef struct codec_basic_resolution_stream {
    /** Bitplanes for this resolution (MSB-first). */
    codec_scan_bitplane* bitplanes;
    /** Number of bitplanes (auto-detected; 0 if all coefficients are 0). */
    int num_bitplanes;
    /** Resolution level (0 = LL, 1..levels = high-pass). */
    int resolution;
} codec_basic_resolution_stream;

/** Encoded data for one image channel (per-resolution streams). */
typedef struct codec_basic_channel_stream {
    /** Per-resolution streams, resolutions[0] = LL, resolutions[levels] =
     * finest.
     */
    codec_basic_resolution_stream* resolutions;
    /** Number of resolution levels = levels + 1. */
    int num_resolutions;
} codec_basic_channel_stream;

/** Encoded representation of a full grayscale or RGB image. */
typedef struct codec_basic_encoded_image {
    int width;
    int height;
    int channels;
    int levels;
    float quant_step;
    /** Color transform: 0 = none, 1 = RCT (RGB↔YCbCr). Always 0 for grayscale.
     */
    int color_transform;
    /** One channel stream per channel. */
    codec_basic_channel_stream* channel_streams;
} codec_basic_encoded_image;

void codec_basic_encoded_init(codec_basic_encoded_image* encoded);
void codec_basic_encoded_free(codec_basic_encoded_image* encoded);

/**
 * @brief Allocates channel and resolution arrays for incremental filling.
 *
 * Frees any prior data in `encoded`, then allocates `channel_streams[channels]`
 * with `num_resolutions = levels + 1` per channel.  All bitplane arrays start
 * NULL with `num_bitplanes = 0`.  Callers append bitplanes by growing the
 * per-resolution arrays and increasing their `num_bitplanes`.
 *
 * @return DIC_STATUS_OK on success.
 */
dic_status codec_basic_encoded_alloc_streams(codec_basic_encoded_image* encoded,
                                             int width, int height,
                                             int channels, int levels,
                                             float quant_step,
                                             int color_transform);

/**
 * @brief Encodes an interleaved 8-bit image with per-resolution bitplane
 * coding.
 *
 * Each resolution level is independently encoded. Resolution 0 is the LL
 * subband; resolution r ≥ 1 contains HL, LH, HH at DWT level (levels - r + 1).
 *
 * @param input      Interleaved source samples in row-major order.
 * @param width      Source width in pixels.
 * @param height     Source height in pixels.
 * @param channels   Source channel count; must be 1 or 3.
 * @param levels     Number of 5/3 DWT decomposition levels.
 * @param quant_step Positive scalar quantization step.
 * @param color_transform 0 = none, 1 = RCT (valid only when channels == 3).
 * @param encoded    Output encoded image. Existing contents are freed.
 * @return DIC_STATUS_OK on success.
 */
dic_status codec_basic_encode_image(const uint8_t* input, int width, int height,
                                    int channels, int levels, float quant_step,
                                    int color_transform,
                                    codec_basic_encoded_image* encoded);

/**
 * @brief Decodes an image with resolution and quality scalability.
 *
 * @param encoded        Encoded image.
 * @param max_resolution Maximum resolution level to decode (0 = LL only,
 *                       levels = full resolution).
 * @param num_bitplanes  Number of bitplanes to decode per resolution
 *                       (1..N, or 0 for all). Midpoint reconstruction is
 *                       applied when fewer than available.
 * @param decoded        Output image; receives allocated sample storage.
 * @return DIC_STATUS_OK on success.
 */
dic_status codec_basic_decode_image(const codec_basic_encoded_image* encoded,
                                    int max_resolution, int num_bitplanes,
                                    dic_image_u8* decoded);

#ifdef __cplusplus
}
#endif

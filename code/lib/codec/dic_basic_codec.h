#pragma once
/**
 * @file dic_basic_codec.h
 * @brief Basic image codec pipeline over 5/3 DWT coefficients.
 *
 * The basic codec converts interleaved 8-bit PGM/PPM samples into per-channel
 * scan-symbol streams. Encoding performs channel separation, reversible 5/3
 * wavelet transform, scalar quantization, LL prediction, and EZT-style scanning.
 * Decoding applies the inverse operations and returns an allocated image.
 */

#include <stddef.h>
#include <stdint.h>

#include "codec/dic_scan.h"
#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_basic_channel_stream
{
    /** Scan symbols for one image channel after DWT, quantization, prediction, and scanning. */
    dic_scan_symbol *symbols;
    /** Number of valid entries in symbols. */
    size_t symbol_count;
} dic_basic_channel_stream;

/** Encoded representation of a full grayscale or RGB image. */
typedef struct dic_basic_encoded_image
{
    /** Source image width in pixels. */
    int width;
    /** Source image height in pixels. */
    int height;
    /** Number of image channels; supported values are 1 and 3. */
    int channels;
    /** Number of 5/3 DWT decomposition levels. */
    int levels;
    /** Positive scalar quantization step used during encoding. */
    int quant_step;
    /** One scan-symbol stream per channel. */
    dic_basic_channel_stream *channel_streams;
} dic_basic_encoded_image;

/**
 * @brief Initializes an encoded-image object to an empty state.
 * @param encoded Encoded-image object to initialize; NULL is ignored.
 */
void dic_basic_encoded_init(dic_basic_encoded_image *encoded);

/**
 * @brief Frees all storage owned by an encoded-image object.
 * @param encoded Encoded-image object to clear; NULL is ignored.
 */
void dic_basic_encoded_free(dic_basic_encoded_image *encoded);

/**
 * @brief Encodes an interleaved 8-bit image into basic codec scan-symbol streams.
 * @param input Interleaved source samples in row-major order.
 * @param width Source width in pixels.
 * @param height Source height in pixels.
 * @param channels Source channel count; must be 1 or 3.
 * @param levels Number of 5/3 DWT decomposition levels.
 * @param quant_step Positive scalar quantization step.
 * @param encoded Output encoded image. Existing contents are freed on success path preparation.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status dic_basic_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int levels,
    int quant_step,
    dic_basic_encoded_image *encoded
);

/**
 * @brief Decodes a basic codec image into newly allocated 8-bit samples.
 * @param encoded Encoded image produced by dic_basic_encode_image() or dic_basic_read_file().
 * @param decoded Output image; receives allocated sample storage on success.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status dic_basic_decode_image(
    const dic_basic_encoded_image *encoded,
    dic_image_u8 *decoded
);

/**
 * @brief Counts scan symbols across all channels in an encoded image.
 * @param encoded Encoded image to inspect.
 * @return Total symbol count, or 0 for NULL or uninitialized input.
 */
size_t dic_basic_symbol_count(const dic_basic_encoded_image *encoded);

#ifdef __cplusplus
}
#endif

#pragma once
/**
 * @file basic_codec.h
 * @brief In-memory API for the full-plane progressive image codec.
 *
 * The basic codec transforms an interleaved PGM/PPM-style sample array into
 * independently progressive channel streams:
 *
 * @code{.unparsed}
 * interleaved u8 image
 *        |
 *        +--> split channels
 *        +--> RGB reversible component transform (RGB only)
 *        +--> multilevel 5/3 DWT
 *        +--> scalar quantization
 *        +--> LL left-neighbor prediction
 *        +--> MSB-to-LSB zerotree bit-plane coding
 *        |
 *        `--> codec_basic_encoded_image
 * @endcode
 *
 * This header describes the in-memory representation. DICW v8 file layout is
 * documented in basic_file.h.
 */

#include <stddef.h>
#include <stdint.h>

#include "codec/scan.h"
#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Progressive bit-plane stream for one transformed image component.
 *
 * Bit-plane element zero is the most significant coded plane. Each element
 * owns its internal dominant, run-length, and refinement byte arrays.
 */
typedef struct codec_basic_channel_stream {
    /** Heap array of @ref codec_scan_bitplane objects, or NULL when empty. */
    codec_scan_bitplane* bitplanes;
    /** Number of valid entries in @ref bitplanes. */
    int num_bitplanes;
} codec_basic_channel_stream;

/**
 * @brief Complete encoded image metadata and per-component streams.
 *
 * The object owns @ref channel_streams and every nested bit-plane allocation.
 * Initialize with codec_basic_encoded_init() and release with
 * codec_basic_encoded_free().
 */
typedef struct codec_basic_encoded_image {
    /** Full-resolution image width in pixels. */
    int width;
    /** Full-resolution image height in pixels. */
    int height;
    /** Component count: one for grayscale or three for RGB-derived data. */
    int channels;
    /** Number of 5/3 wavelet decomposition levels. */
    int levels;
    /** Positive scalar quantization step stored in the bitstream. */
    float quant_step;
    /** Heap array containing @ref channels component streams. */
    codec_basic_channel_stream* channel_streams;
} codec_basic_encoded_image;

/**
 * @brief Resets an encoded-image object to an empty non-owning state.
 * @param encoded Object to initialize; NULL is accepted.
 */
void codec_basic_encoded_init(codec_basic_encoded_image* encoded);

/**
 * @brief Releases all nested bit-plane data and resets the object.
 * @param encoded Object to release; NULL is accepted.
 */
void codec_basic_encoded_free(codec_basic_encoded_image* encoded);

/**
 * @brief Allocates empty channel stream descriptors and records image metadata.
 *
 * Existing contents of @p encoded are released first. Bit-plane arrays are not
 * allocated by this function.
 *
 * @param encoded Destination object.
 * @param width Positive image width.
 * @param height Positive image height.
 * @param channels One or three.
 * @param levels Valid 5/3 DWT decomposition level count.
 * @param quant_step Finite positive quantization step.
 * @return DIC_STATUS_OK on success, otherwise a validation or allocation
 * error.
 */
dic_status codec_basic_encoded_alloc_streams(codec_basic_encoded_image* encoded,
                                             int width, int height,
                                             int channels, int levels,
                                             float quant_step);

/**
 * @brief Encodes an interleaved 8-bit image into progressive bit-plane streams.
 *
 * RGB input always uses the reversible component transform. Grayscale input
 * remains unchanged. On failure, @p encoded is left empty.
 *
 * @param input Row-major interleaved samples containing
 * `width * height * channels` bytes.
 * @param width Image width.
 * @param height Image height.
 * @param channels One for grayscale or three for RGB.
 * @param levels Number of 5/3 DWT decomposition levels.
 * @param quant_step Finite positive scalar quantization step.
 * @param encoded Destination owning object.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_encode_image(const uint8_t* input, int width, int height,
                                    int channels, int levels, float quant_step,
                                    codec_basic_encoded_image* encoded);

/**
 * @brief Reconstructs an image from the first quality bit-planes.
 *
 * @param encoded Valid encoded image.
 * @param num_bitplanes Number of MSB-first planes to decode per channel.
 * Zero means all available planes. Values larger than a channel's count are
 * clamped to that count.
 * @param decoded Destination image. The function allocates decoded->data;
 * release it with dic_image_u8_free().
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_decode_image(const codec_basic_encoded_image* encoded,
                                    int num_bitplanes, dic_image_u8* decoded);

/**
 * @brief Sums Huffman command frequencies across all channels and bitplanes.
 */
void codec_basic_huffman_symbol_counts(const codec_basic_encoded_image* encoded,
                                       size_t counts[DIC_SCAN_TOKEN_COUNT]);

#ifdef __cplusplus
}
#endif

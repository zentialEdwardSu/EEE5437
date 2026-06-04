#pragma once
/**
 * @file scan.h
 * @brief EZW-style per-bitplane zerotree scan for progressive bitplane coding.
 *
 * Coefficients are encoded bitplane by bitplane (T = 2^bp) from MSB to LSB.
 * The number of bitplanes is auto-detected from the maximum coefficient
 * magnitude.  Each bitplane stores:
 *   - A significance (dominant) pass with EZT: Huffman-coded IZ / ZTR / POS / NEG
 *   - A refinement (subordinate) pass: raw packed bits for already-significant
 *     coefficients
 *
 * Progressive decoding is achieved by decoding only the first N bitplanes,
 * with midpoint reconstruction applied for the first missing bitplane.
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"
#include "hw2/hw2_huffman.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Dominant-pass token emitted during the significance pass of each bitplane. */
typedef enum codec_scan_token
{
    /** Isolated zero — insignificant at T, but at least one descendant is significant. */
    DIC_SCAN_TOKEN_IZ = 0,
    /** Zerotree root — insignificant at T, and all descendants are also insignificant. */
    DIC_SCAN_TOKEN_ZTR = 1,
    /** Positive significant — |coeff| >= T, sign positive. */
    DIC_SCAN_TOKEN_POS = 2,
    /** Negative significant — |coeff| >= T, sign negative. */
    DIC_SCAN_TOKEN_NEG = 3,
    /** Number of token kinds (Huffman alphabet size). */
    DIC_SCAN_TOKEN_COUNT = 4
} codec_scan_token;

/** Encoded data for a single bitplane (threshold T = 2^bp). */
typedef struct codec_scan_bitplane
{
    /** Number of dominant-pass tokens emitted at this bitplane. */
    size_t dominant_token_count;
    /** Token frequencies (IZ, ZTR, POS, NEG) used to rebuild the Huffman tree. */
    size_t token_freq[DIC_SCAN_TOKEN_COUNT];
    /** Huffman-coded dominant-pass token bitstream. */
    dic_hw2_huffman_bitstream dominant_stream;
    /** Raw packed refinement bits. */
    unsigned char *subordinate_bits;
    /** Number of valid refinement bits (not bytes). */
    size_t subordinate_bit_count;
    /** ceil(subordinate_bit_count / 8). */
    size_t subordinate_byte_count;
} codec_scan_bitplane;

/**
 * @brief Initializes a bitplane to an empty state.
 * @param bp Bitplane to initialize; NULL is ignored.
 */
void codec_scan_bitplane_init(codec_scan_bitplane *bp);

/**
 * @brief Frees all storage owned by a bitplane.
 * @param bp Bitplane to clear; NULL is ignored.
 */
void codec_scan_bitplane_free(codec_scan_bitplane *bp);

/**
 * @brief Encodes a quantized coefficient plane with per-bitplane EZT.
 *
 * The number of bitplanes is auto-detected from max|coefficient|.
 * If all coefficients are zero, *bitplane_count_out is set to 0 and
 * *bitplanes_out is set to NULL.
 *
 * @param plane      Quantized coefficient plane in packed subband layout.
 * @param width      Plane width.
 * @param height     Plane height.
 * @param levels     Number of DWT decomposition levels.
 * @param bitplanes_out  Output array of bitplanes (MSB-first); caller frees
 *                       each with codec_scan_bitplane_free() then frees the array.
 * @param bitplane_count_out  Number of bitplanes written (0 if all-zero plane).
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_scan_encode_plane(
    const int32_t *plane,
    int width,
    int height,
    int levels,
    codec_scan_bitplane **bitplanes_out,
    int *bitplane_count_out);

/**
 * @brief Decodes the first num_bitplanes of per-bitplane EZT data into a
 * coefficient plane.
 *
 * Midpoint reconstruction is applied when num_bitplanes < total_bitplane_count:
 * the first missing bitplane contributes 2^(bp-1) to each significant coefficient.
 *
 * @param bitplanes             Array of bitplanes (MSB-first).
 * @param total_bitplane_count  Total number of bitplanes available.
 * @param num_bitplanes         Number of bitplanes to decode (1..total).
 * @param width                 Plane width.
 * @param height                Plane height.
 * @param levels                Number of DWT decomposition levels.
 * @param plane                 Output coefficient plane (zeroed on entry).
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_scan_decode_plane(
    const codec_scan_bitplane *bitplanes,
    int total_bitplane_count,
    int num_bitplanes,
    int width,
    int height,
    int levels,
    int32_t *plane);

/**
 * @brief Returns the number of magnitude bits needed to represent a signed amplitude.
 * @param amplitude Signed coefficient amplitude.
 * @return Bit width, or 0 when amplitude is zero.
 */
unsigned char codec_scan_amplitude_size(int32_t amplitude);

#ifdef __cplusplus
}
#endif

#pragma once
/**
 * @file scan.h
 * @brief EZT-style scan-symbol encoding for quantized wavelet planes.
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Symbol kind emitted by the coefficient scanner. */
typedef enum codec_scan_symbol_kind
{
    /** Single zero coefficient. */
    DIC_SCAN_SYMBOL_ZERO = 0,
    /** Embedded zerotree marker covering a zero coefficient and zero descendants. */
    DIC_SCAN_SYMBOL_EZT = 1,
    /** Nonzero coefficient with stored amplitude. */
    DIC_SCAN_SYMBOL_NONZERO = 2
} codec_scan_symbol_kind;

/** One scanned coefficient token. */
typedef struct codec_scan_symbol
{
    /** Symbol kind from codec_scan_symbol_kind. */
    unsigned char kind;
    /** Bit width of amplitude for nonzero symbols, otherwise 0. */
    unsigned char size;
    /** Signed coefficient amplitude for nonzero symbols, otherwise 0. */
    int32_t amplitude;
} codec_scan_symbol;

/** Growable buffer of scan symbols. */
typedef struct codec_scan_symbol_buffer
{
    /** Allocated symbol storage. */
    codec_scan_symbol *symbols;
    /** Number of valid symbols. */
    size_t count;
    /** Allocated symbol capacity. */
    size_t capacity;
} codec_scan_symbol_buffer;

/**
 * @brief Initializes a scan-symbol buffer to an empty state.
 * @param buffer Buffer to initialize; NULL is ignored.
 */
void codec_scan_symbol_buffer_init(codec_scan_symbol_buffer *buffer);

/**
 * @brief Frees storage owned by a scan-symbol buffer.
 * @param buffer Buffer to clear; NULL is ignored.
 */
void codec_scan_symbol_buffer_free(codec_scan_symbol_buffer *buffer);

/**
 * @brief Encodes a quantized wavelet coefficient plane into scan symbols.
 * @param plane Quantized coefficient plane in packed subband layout.
 * @param width Plane width.
 * @param height Plane height.
 * @param levels Number of DWT decomposition levels.
 * @param symbols Output symbol buffer; existing contents are freed before use.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_scan_encode_plane(
    const int32_t *plane,
    int width,
    int height,
    int levels,
    codec_scan_symbol_buffer *symbols
);

/**
 * @brief Decodes scan symbols back into a quantized wavelet coefficient plane.
 * @param symbols Input symbol stream.
 * @param symbol_count Number of input symbols.
 * @param width Plane width.
 * @param height Plane height.
 * @param levels Number of DWT decomposition levels.
 * @param plane Output coefficient plane in packed subband layout.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_scan_decode_plane(
    const codec_scan_symbol *symbols,
    size_t symbol_count,
    int width,
    int height,
    int levels,
    int32_t *plane
);

/**
 * @brief Encodes coefficients in the subbands of one resolution level with
 * per-bitplane significance + refinement passes (no zerotree).
 *
 * Resolution 0 encodes the LL subband only. Resolution r ≥ 1 encodes the
 * three high-pass subbands (HL, LH, HH) at DWT level (levels - r + 1).
 *
 * The number of bitplanes is auto-detected from the maximum coefficient
 * magnitude within the encoded subbands. If all coefficients are zero,
 * *bitplane_count_out is set to 0 and *bitplanes_out is set to NULL.
 *
 * @param plane      Quantized coefficient plane in packed subband layout.
 * @param width      Full plane width.
 * @param height     Full plane height.
 * @param levels     Total DWT decomposition levels.
 * @param resolution Resolution level to encode (0..levels).
 * @param bitplanes_out  Output array of bitplanes (MSB-first).
 * @param bitplane_count_out  Number of bitplanes written.
 * @return DIC_STATUS_OK on success.
 */
dic_status codec_scan_encode_subbands(
    const int32_t *plane,
    int width,
    int height,
    int levels,
    int resolution,
    codec_scan_bitplane **bitplanes_out,
    int *bitplane_count_out);

/**
 * @brief Decodes per-resolution bitplane data and places coefficients into
 * a plane for a max_resolution-level IDWT.
 *
 * Midpoint reconstruction is applied when decode_bitplanes < total_bitplane_count.
 *
 * @param bitplanes           Array of bitplanes (MSB-first) for this resolution.
 * @param total_bitplane_count  Number of bitplanes available.
 * @param decode_bitplanes    Number of bitplanes to decode (1..total, or total for all).
 * @param out_width           Output plane width (ll_w << max_resolution).
 * @param out_height          Output plane height (ll_h << max_resolution).
 * @param max_resolution      Total resolution levels for the output plane.
 * @param resolution          Which resolution this data represents (0..max_resolution).
 * @param plane               Output coefficient plane (zeroed on entry).
 * @return DIC_STATUS_OK on success.
 */
dic_status codec_scan_decode_subbands(
    const codec_scan_bitplane *bitplanes,
    int total_bitplane_count,
    int decode_bitplanes,
    int out_width,
    int out_height,
    int max_resolution,
    int resolution,
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

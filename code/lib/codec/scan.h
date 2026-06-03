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
 * @brief Returns the number of magnitude bits needed to represent a signed amplitude.
 * @param amplitude Signed coefficient amplitude.
 * @return Bit width, or 0 when amplitude is zero.
 */
unsigned char codec_scan_amplitude_size(int32_t amplitude);

#ifdef __cplusplus
}
#endif

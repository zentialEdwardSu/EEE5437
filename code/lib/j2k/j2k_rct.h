#pragma once

/**
 * @file j2k_rct.h
 * @brief JPEG 2000 Reversible Component Transform (RCT) declarations.
 *
 * Implements T.800 Annex G.1 — the reversible colour transform that maps
 * interleaved unsigned RGB samples to the decorrelated YDbDr space before
 * wavelet transformation. The RCT is the lossless path's component
 * transform; it is always paired with the reversible 5-3 DWT (Annex F.3).
 *
 * Forward equations (T.800 Eq. G-1 through G-3):
 * @code
 *   Y  = floor((R + 2G + B) / 4)
 *   Db = B - G
 *   Dr = R - G
 * @endcode
 *
 * Inverse equations (T.800 Eq. G-4 through G-6):
 * @code
 *   G = Y - floor((Db + Dr) / 4)
 *   R = Dr + G
 *   B = Db + G
 * @endcode
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex G.1
 * - j2k_rct.c for the implementation
 * - j2k_image.c for encoder integration (COD MCT signalling)
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Apply the forward RCT to interleaved RGB sample triples.
 *
 * Transforms each (R,G,B) triple in-place to (Y,Db,Dr) using the
 * reversible integer equations from T.800 Annex G.1. The computation
 * uses exact integer arithmetic — the floor-divide-by-4 is performed via
 * arithmetic right-shift-compatible rounding for negative values.
 *
 * @param samples Interleaved int32_t samples in R,G,B,R,G,B,... order.
 *                Must be non-NULL. Modified in-place to Y,Db,Dr order.
 * @param pixel_count Number of RGB triples (not individual samples).
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p samples is NULL.
 */
dic_status j2k_rct_forward(
    int32_t *samples,
    size_t pixel_count
);

/**
 * @brief Apply the inverse RCT to interleaved YDbDr sample triples.
 *
 * Reconstructs (R,G,B) from (Y,Db,Dr) in-place using the reversible
 * integer equations from T.800 Annex G.1. The inverse is the algebraic
 * inverse of the forward transform — a round-trip is bit-exact.
 *
 * @param samples Interleaved int32_t samples in Y,Db,Dr,Y,Db,Dr,...
 *                order. Must be non-NULL. Modified in-place to R,G,B order.
 * @param pixel_count Number of YDbDr triples (not individual samples).
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p samples is NULL.
 */
dic_status j2k_rct_inverse(
    int32_t *samples,
    size_t pixel_count
);

#ifdef __cplusplus
}
#endif

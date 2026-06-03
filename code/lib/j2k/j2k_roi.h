#pragma once

/**
 * @file j2k_roi.h
 * @brief JPEG 2000 Region-of-Interest (ROI) Maxshift declarations.
 *
 * Implements T.800 Annex H — the Maxshift ROI coding method. ROI Maxshift
 * scales up wavelet coefficients belonging to a specified image-domain
 * rectangular region so that their most significant bit-planes land in
 * higher-quality layers during embedded bit-plane coding. This allows the
 * ROI to be decoded losslessly (or at higher quality) before the background
 * region.
 *
 * The encoder pipeline:
 *   1. j2k_roi_build_shift_map() traces an image-domain ROI rectangle
 *      backwards through the inverse 5-3 synthesis dependencies (Annex
 *      H.3.1.1) to produce a coefficient-domain binary mask.
 *   2. j2k_roi_apply_shift_map() left-shifts each masked coefficient by
 *      the RGN-specified scaling value (SPrgn), applying the magnitude
 *      scaling defined in Annex H.4.
 *   3. The RGN marker segment (Annex A.8.4, Tables A.24-A.26) signals
 *      the Maxshift style and shift value to the decoder.
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex H (ROI coding)
 * - paper/T-REC-T.800-200208.pdf, Annex H.3.1.1 (5-3 synthesis dependency trace)
 * - paper/T-REC-T.800-200208.pdf, Annex A.8.4 (RGN marker segment)
 */

#include <stdint.h>

#include "codec/subband.h"
#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build a coefficient-domain Maxshift mask from an image-domain ROI.
 *
 * Traces the rectangular image-domain ROI backwards through @p levels of
 * reversible 5-3 wavelet synthesis dependencies. Each synthesis step
 * produces a coefficient-domain mask that is OR'd into the accumulated
 * result. The output @p shift_map is a caller-owned byte array of size
 * @p width × @p height; entries set to 1 indicate coefficients that
 * contribute to the ROI and should receive Maxshift scaling.
 *
 * If the ROI rectangle does not intersect the image bounds, the output
 * shift_map is set to NULL and DIC_STATUS_OK is returned (a no-op ROI).
 *
 * @param width Image width in pixels (and coefficient plane columns).
 * @param height Image height in pixels (and coefficient plane rows).
 * @param levels Number of 5-3 DWT decomposition levels.
 * @param roi_rect Image-domain ROI rectangle; NULL means no ROI.
 * @param shift_map [out] Receives a newly allocated uint8_t array of
 *                 @p width × @p height entries. Caller owns the memory
 *                 and must free() it. Set to NULL if ROI is empty.
 * @return DIC_STATUS_OK on success (including empty ROI).
 * @return DIC_STATUS_INVALID_ARGUMENT for invalid dimensions or NULL
 *         shift_map pointer.
 * @return DIC_STATUS_MEMORY_ERROR if allocation fails.
 */
dic_status j2k_roi_build_shift_map(
    int width,
    int height,
    int levels,
    const dic_rect_i32 *roi_rect,
    uint8_t **shift_map
);

/**
 * @brief Apply Maxshift magnitude scaling to coefficients in the ROI mask.
 *
 * For each coefficient plane sample where @p shift_map is non-zero, the
 * coefficient magnitude is left-shifted by @p shift bits. This moves the
 * ROI coefficient's most significant bit-planes into higher-quality
 * layers during subsequent EBCOT rate allocation and layer construction.
 *
 * If @p shift_map is NULL or @p shift is 0, this is a no-op returning
 * DIC_STATUS_OK.
 *
 * @param plane Coefficient plane in row-major order, modified in-place.
 * @param width Coefficient plane width.
 * @param height Coefficient plane height.
 * @param shift_map Binary mask from j2k_roi_build_shift_map();
 *                 non-zero entries indicate ROI coefficients.
 * @param shift Number of bit-planes to left-shift (the SPrgn value).
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p plane is NULL,
 *         dimensions are invalid, or shift would overflow int32_t.
 */
dic_status j2k_roi_apply_shift_map(
    int32_t *plane,
    int width,
    int height,
    const uint8_t *shift_map,
    uint8_t shift
);

#ifdef __cplusplus
}
#endif

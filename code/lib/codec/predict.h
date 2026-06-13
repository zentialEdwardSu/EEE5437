#pragma once
/**
 * @file predict.h
 * @brief Horizontal operation for the lowest LL wavelet subband.
 *
 * Prediction is reset at each row boundary:
 *
 * @code{.unparsed}
 * source row:    100 101 103 106
 * residual row: 100   1   2   3
 *
 * packed coefficient plane
 * +-------------------+----------------------+
 * | LL rectangle      | untouched subbands   |
 * | processed by row  |                      |
 * +-------------------+----------------------+
 * | untouched subbands                       |
 * +-------------------------------------------+
 * @endcode
 */

#include <stdint.h>

#include "codec/subband.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Replaces each coefficient in the LL rectangle with a left-difference residual.
 * @param plane Coefficient plane in row-major order.
 * @param stride Number of coefficients per image row.
 * @param rect LL rectangle to predict.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_predict_ll_left(
    int32_t *plane,
    int stride,
    dic_rect_i32 rect
);

/**
 * @brief Reconstructs LL coefficients from left-difference residuals.
 * @param plane Coefficient plane in row-major order.
 * @param stride Number of coefficients per image row.
 * @param rect LL rectangle to unpredict.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_unpredict_ll_left(
    int32_t *plane,
    int stride,
    dic_rect_i32 rect
);

#ifdef __cplusplus
}
#endif

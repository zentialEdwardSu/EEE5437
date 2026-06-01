#pragma once
/**
 * @file dic_predict.h
 * @brief Predictive coding helpers for the lowest LL wavelet subband.
 */

#include <stdint.h>

#include "codec/dic_subband.h"

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
dic_status dic_predict_ll_left(
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
dic_status dic_unpredict_ll_left(
    int32_t *plane,
    int stride,
    dic_rect_i32 rect
);

#ifdef __cplusplus
}
#endif

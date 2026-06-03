#pragma once
/**
 * @file quant.h
 * @brief Scalar quantization helpers for signed 32-bit coefficients.
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Quantizes signed coefficients by a positive scalar step with symmetric rounding.
 * @param values Coefficients to modify in place.
 * @param count Number of coefficients.
 * @param step_size Positive quantization step.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_quant_scalar_i32(
    int32_t *values,
    size_t count,
    int step_size
);

/**
 * @brief Dequantizes signed coefficients by multiplying by the scalar step.
 * @param values Coefficients to modify in place.
 * @param count Number of coefficients.
 * @param step_size Positive quantization step used during encoding.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_dequant_scalar_i32(
    int32_t *values,
    size_t count,
    int step_size
);

#ifdef __cplusplus
}
#endif

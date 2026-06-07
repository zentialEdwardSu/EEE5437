/**
 * @file quant.c
 * @brief Implements scalar quantization and dequantization for codec coefficients.
 */

#include "codec/quant.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>

static int _round_symmetric_i32(double value, int32_t* rounded) {
    double magnitude;
    double result;

    if (rounded == NULL || !isfinite(value)) return 0;
    magnitude = floor(fabs(value) + 0.5);
    result = value < 0.0 ? -magnitude : magnitude;
    if (result < (double)INT32_MIN || result > (double)INT32_MAX) return 0;
    *rounded = (int32_t)result;
    return 1;
}

dic_status codec_quant_scalar_i32(int32_t* values, size_t count,
                                  float step_size) {
    size_t i;

    if ((values == NULL && count > 0u) || !isfinite(step_size) ||
        step_size <= 0.0f)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (i = 0; i < count; ++i) {
        if (!_round_symmetric_i32((double)values[i] / (double)step_size,
                                       values + i))
            return DIC_STATUS_INVALID_ARGUMENT;
    }

    return DIC_STATUS_OK;
}

dic_status codec_dequant_scalar_i32(int32_t* values, size_t count,
                                    float step_size) {
    size_t i;

    if ((values == NULL && count > 0u) || !isfinite(step_size) ||
        step_size <= 0.0f)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (i = 0; i < count; ++i) {
        if (!_round_symmetric_i32((double)values[i] * (double)step_size,
                                       values + i))
            return DIC_STATUS_INVALID_ARGUMENT;
    }

    return DIC_STATUS_OK;
}

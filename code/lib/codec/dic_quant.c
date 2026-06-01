/**
 * @file dic_quant.c
 * @brief Implements scalar quantization and dequantization for codec coefficients.
 */

#include "codec/dic_quant.h"

#include <stddef.h>

static int32_t dic_quant_round_div(int32_t value, int step_size)
{
    int32_t step = (int32_t)step_size;

    if (step_size <= 1)
        return value;
    if (value >= 0)
        return (value + (step / 2)) / step;
    return -(((-value) + (step / 2)) / step);
}

dic_status dic_quant_scalar_i32(int32_t *values, size_t count, int step_size)
{
    size_t i;

    if ((values == NULL && count > 0u) || step_size <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (i = 0; i < count; ++i)
        values[i] = dic_quant_round_div(values[i], step_size);

    return DIC_STATUS_OK;
}

dic_status dic_dequant_scalar_i32(int32_t *values, size_t count, int step_size)
{
    size_t i;

    if ((values == NULL && count > 0u) || step_size <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    if (step_size == 1)
        return DIC_STATUS_OK;

    for (i = 0; i < count; ++i)
        values[i] *= (int32_t)step_size;

    return DIC_STATUS_OK;
}

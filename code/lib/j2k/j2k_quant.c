/**
 * @file j2k_quant.c
 * @brief Implements JPEG 2000 irreversible scalar quantization helpers.
 *
 * The routines here avoid codestream writer assumptions. SPqcd conversion uses
 * the normalized irreversible 5-bit exponent and 11-bit mantissa representation
 * so later codestream code can apply subband and component context explicitly.
 */

#include "j2k/j2k_quant.h"

#include <limits.h>
#include <math.h>

#define j2k_QUANT_MIN_QUALITY 1
#define j2k_QUANT_MAX_QUALITY 100
#define j2k_QUANT_SPQCD_MANTISSA_BITS 11
#define j2k_QUANT_SPQCD_MANTISSA_MAX 0x7ffu
#define j2k_QUANT_SPQCD_MANTISSA_SCALE 2048.0
#define j2k_QUANT_SPQCD_EXPONENT_MAX 31

/**
 * @brief Return whether a floating point step is finite and positive.
 */
static int j2k_quant_is_positive_finite(double value)
{
    return isfinite(value) && value > 0.0;
}

dic_status j2k_quant_validate_lossy_quality(int quality)
{
    if (quality < j2k_QUANT_MIN_QUALITY || quality > j2k_QUANT_MAX_QUALITY)
        return DIC_STATUS_INVALID_ARGUMENT;
    return DIC_STATUS_OK;
}

dic_status j2k_quant_base_step_from_quality(int quality, double *base_step)
{
    if (base_step == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (j2k_quant_validate_lossy_quality(quality) != DIC_STATUS_OK)
        return DIC_STATUS_INVALID_ARGUMENT;

    *base_step = (double)(j2k_QUANT_MAX_QUALITY + 1 - quality) / (double)j2k_QUANT_MAX_QUALITY;
    return DIC_STATUS_OK;
}

dic_status j2k_quantize_coefficient(double coefficient, double step_size, int32_t *quantized)
{
    double rounded;

    if (quantized == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (!isfinite(coefficient) || !j2k_quant_is_positive_finite(step_size))
        return DIC_STATUS_INVALID_ARGUMENT;

    rounded = round(coefficient / step_size);
    if (rounded < (double)INT32_MIN || rounded > (double)INT32_MAX)
        return DIC_STATUS_INVALID_ARGUMENT;

    *quantized = (int32_t)rounded;
    return DIC_STATUS_OK;
}

dic_status j2k_dequantize_coefficient(int32_t quantized, double step_size, double *coefficient)
{
    if (coefficient == NULL || !j2k_quant_is_positive_finite(step_size))
        return DIC_STATUS_INVALID_ARGUMENT;

    *coefficient = (double)quantized * step_size;
    if (!isfinite(*coefficient))
        return DIC_STATUS_INVALID_ARGUMENT;
    return DIC_STATUS_OK;
}

dic_status j2k_quant_encode_irreversible_spqcd(
    double step_size,
    unsigned int range_bits,
    uint16_t *spqcd
)
{
    double exponent_value;
    double base;
    double mantissa_value;
    int exponent;
    unsigned int mantissa;

    if (spqcd == NULL || !j2k_quant_is_positive_finite(step_size) || range_bits > 31u)
        return DIC_STATUS_INVALID_ARGUMENT;

    exponent_value = ceil((double)range_bits - log2(step_size));
    if (!isfinite(exponent_value))
        return DIC_STATUS_INVALID_ARGUMENT;
    exponent = (int)exponent_value;
    if (exponent < 0 || exponent > j2k_QUANT_SPQCD_EXPONENT_MAX)
        return DIC_STATUS_INVALID_ARGUMENT;

    base = ldexp(1.0, (int)range_bits - exponent);
    mantissa_value = round(((step_size / base) - 1.0) * j2k_QUANT_SPQCD_MANTISSA_SCALE);
    if (mantissa_value < 0.0)
        mantissa_value = 0.0;
    if (mantissa_value >= j2k_QUANT_SPQCD_MANTISSA_SCALE)
    {
        --exponent;
        mantissa_value = 0.0;
        if (exponent < 0)
            return DIC_STATUS_INVALID_ARGUMENT;
    }

    mantissa = (unsigned int)mantissa_value;
    *spqcd = (uint16_t)(((uint16_t)exponent << j2k_QUANT_SPQCD_MANTISSA_BITS)
        | (uint16_t)(mantissa & j2k_QUANT_SPQCD_MANTISSA_MAX));
    return DIC_STATUS_OK;
}

dic_status j2k_quant_decode_irreversible_spqcd(
    uint16_t spqcd,
    unsigned int range_bits,
    double *step_size
)
{
    unsigned int exponent;
    unsigned int mantissa;

    if (step_size == NULL || range_bits > 31u)
        return DIC_STATUS_INVALID_ARGUMENT;

    exponent = (unsigned int)(spqcd >> j2k_QUANT_SPQCD_MANTISSA_BITS);
    mantissa = (unsigned int)(spqcd & j2k_QUANT_SPQCD_MANTISSA_MAX);
    *step_size = (1.0 + ((double)mantissa / j2k_QUANT_SPQCD_MANTISSA_SCALE))
        * ldexp(1.0, (int)range_bits - (int)exponent);
    return DIC_STATUS_OK;
}

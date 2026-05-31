#pragma once

/**
 * @file j2k_quant.h
 * @brief JPEG 2000 irreversible scalar quantization helper declarations.
 *
 * This module is intentionally independent from codestream writing. It exposes
 * normalized lossy quality-to-step mapping, scalar coefficient quantization,
 * and T.800 SPqcd irreversible step-size field conversion.
 */

#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validate that a quality value selects lossy JPEG 2000 quantization.
 *
 * Quality values 1 through 100 are accepted. The project-level lossless
 * sentinel -1 is deliberately rejected here because this module only generates
 * irreversible lossy quantization steps.
 *
 * @param quality User quality value to validate.
 * @return DIC_STATUS_OK for lossy quality, otherwise DIC_STATUS_INVALID_ARGUMENT.
 */
dic_status j2k_quant_validate_lossy_quality(int quality);

/**
 * @brief Map lossy quality 1 through 100 to a positive normalized base step.
 *
 * Higher quality produces a smaller or equal step. The returned step is a
 * normalized scalar helper value, not a subband-adjusted codestream parameter.
 *
 * @param quality Lossy quality in the inclusive range [1, 100].
 * @param base_step Receives the positive normalized base step.
 * @return DIC_STATUS_OK on success, otherwise DIC_STATUS_INVALID_ARGUMENT.
 */
dic_status j2k_quant_base_step_from_quality(int quality, double *base_step);

/**
 * @brief Quantize one coefficient with round-to-nearest, half away from zero.
 *
 * The rounding mode is the C standard library `round` behavior applied to
 * coefficient divided by step size.
 *
 * @param coefficient Input transform coefficient.
 * @param step_size Positive quantization step size.
 * @param quantized Receives the quantized signed coefficient.
 * @return DIC_STATUS_OK on success, otherwise DIC_STATUS_INVALID_ARGUMENT.
 */
dic_status j2k_quantize_coefficient(double coefficient, double step_size, int32_t *quantized);

/**
 * @brief Dequantize one signed coefficient by multiplying by step size.
 *
 * @param quantized Quantized signed coefficient.
 * @param step_size Positive quantization step size.
 * @param coefficient Receives the reconstructed coefficient value.
 * @return DIC_STATUS_OK on success, otherwise DIC_STATUS_INVALID_ARGUMENT.
 */
dic_status j2k_dequantize_coefficient(int32_t quantized, double step_size, double *coefficient);

/**
 * @brief Encode an irreversible step size as a 16-bit SPqcd field.
 *
 * The field packs a 5-bit exponent and 11-bit mantissa using the T.800
 * relation step = (1 + mantissa / 2048) * 2^(range_bits - exponent).
 *
 * @param step_size Positive irreversible quantization step size.
 * @param range_bits Sub-band dynamic range Rb from T.800 Annex E.
 * @param spqcd Receives the packed SPqcd field.
 * @return DIC_STATUS_OK on success, otherwise DIC_STATUS_INVALID_ARGUMENT.
 */
dic_status j2k_quant_encode_irreversible_spqcd(
    double step_size,
    unsigned int range_bits,
    uint16_t *spqcd
);

/**
 * @brief Decode a 16-bit irreversible SPqcd field to a normalized step size.
 *
 * @param spqcd Packed SPqcd field with 5-bit exponent and 11-bit mantissa.
 * @param range_bits Sub-band dynamic range Rb from T.800 Annex E.
 * @param step_size Receives the decoded positive normalized step size.
 * @return DIC_STATUS_OK on success, otherwise DIC_STATUS_INVALID_ARGUMENT.
 */
dic_status j2k_quant_decode_irreversible_spqcd(
    uint16_t spqcd,
    unsigned int range_bits,
    double *step_size
);

#ifdef __cplusplus
}
#endif

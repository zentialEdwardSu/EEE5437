/**
 * @file lib_j2k_test_quant.c
 * @brief Tests JPEG 2000 irreversible scalar quantization helpers.
 */

#include <math.h>
#include <stdint.h>

#include "j2k/j2k_quant.h"
#include "test_helpers.h"

static void dic_expect_near(double actual, double expected, double tolerance)
{
    DIC_EXPECT(fabs(actual - expected) <= tolerance);
}

int main(void)
{
    double q1_step;
    double q50_step;
    double q100_step;
    double dequantized;
    double decoded_step;
    uint16_t spqcd;
    int32_t quantized;
    int quality;

    DIC_EXPECT(j2k_quant_validate_lossy_quality(1) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_quant_validate_lossy_quality(100) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_quant_validate_lossy_quality(-1) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quant_validate_lossy_quality(0) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quant_validate_lossy_quality(101) == DIC_STATUS_INVALID_ARGUMENT);

    DIC_EXPECT(j2k_quant_base_step_from_quality(1, &q1_step) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_quant_base_step_from_quality(50, &q50_step) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_quant_base_step_from_quality(100, &q100_step) == DIC_STATUS_OK);
    DIC_EXPECT(q1_step > 0.0);
    DIC_EXPECT(q50_step > 0.0);
    DIC_EXPECT(q100_step > 0.0);
    DIC_EXPECT(q1_step >= q50_step);
    DIC_EXPECT(q50_step >= q100_step);

    for (quality = 1; quality < 100; ++quality)
    {
        double lower_quality_step;
        double higher_quality_step;

        DIC_EXPECT(j2k_quant_base_step_from_quality(quality, &lower_quality_step) == DIC_STATUS_OK);
        DIC_EXPECT(j2k_quant_base_step_from_quality(quality + 1, &higher_quality_step) == DIC_STATUS_OK);
        DIC_EXPECT(lower_quality_step >= higher_quality_step);
    }

    DIC_EXPECT(j2k_quant_base_step_from_quality(-1, &q1_step) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quant_base_step_from_quality(0, &q1_step) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quant_base_step_from_quality(101, &q1_step) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quant_base_step_from_quality(50, NULL) == DIC_STATUS_INVALID_ARGUMENT);

    DIC_EXPECT(j2k_quantize_coefficient(5.0, 2.0, &quantized) == DIC_STATUS_OK);
    DIC_EXPECT(quantized == 2);
    DIC_EXPECT(j2k_quantize_coefficient(-5.0, 2.0, &quantized) == DIC_STATUS_OK);
    DIC_EXPECT(quantized == -2);
    DIC_EXPECT(j2k_quantize_coefficient(2.0, 2.0, &quantized) == DIC_STATUS_OK);
    DIC_EXPECT(quantized == 1);
    DIC_EXPECT(j2k_quantize_coefficient(1.0, 2.0, &quantized) == DIC_STATUS_OK);
    DIC_EXPECT(quantized == 0);
    DIC_EXPECT(j2k_dequantize_coefficient(-3, 2.0, &dequantized) == DIC_STATUS_OK);
    dic_expect_near(dequantized, -7.0, 0.0);
    DIC_EXPECT(j2k_dequantize_coefficient(0, 2.0, &dequantized) == DIC_STATUS_OK);
    dic_expect_near(dequantized, 0.0, 0.0);

    DIC_EXPECT(j2k_quantize_coefficient(1.0, 0.0, &quantized) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quantize_coefficient(1.0, -1.0, &quantized) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quantize_coefficient(1.0, 1.0, NULL) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_dequantize_coefficient(1, 0.0, &dequantized) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_dequantize_coefficient(1, 1.0, NULL) == DIC_STATUS_INVALID_ARGUMENT);

    DIC_EXPECT(j2k_quant_encode_irreversible_spqcd(0.5, 8u, &spqcd) == DIC_STATUS_OK);
    DIC_EXPECT((spqcd >> 11) == 9u);
    DIC_EXPECT(j2k_quant_decode_irreversible_spqcd(spqcd, 8u, &decoded_step) == DIC_STATUS_OK);
    dic_expect_near(decoded_step, 0.5, 1.0 / 2048.0);

    DIC_EXPECT(j2k_quant_encode_irreversible_spqcd(0.75, 9u, &spqcd) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_quant_decode_irreversible_spqcd(spqcd, 9u, &decoded_step) == DIC_STATUS_OK);
    dic_expect_near(decoded_step, 0.75, 1.0 / 2048.0);

    DIC_EXPECT(j2k_quant_encode_irreversible_spqcd(1.0 / 1024.0, 10u, &spqcd) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_quant_decode_irreversible_spqcd(spqcd, 10u, &decoded_step) == DIC_STATUS_OK);
    dic_expect_near(decoded_step, 1.0 / 1024.0, 1.0 / 2048.0);

    DIC_EXPECT(j2k_quant_encode_irreversible_spqcd(0.0, 8u, &spqcd) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quant_encode_irreversible_spqcd(-0.25, 8u, &spqcd) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quant_encode_irreversible_spqcd(0.5, 32u, &spqcd) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quant_encode_irreversible_spqcd(0.5, 8u, NULL) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quant_decode_irreversible_spqcd(spqcd, 32u, &decoded_step) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_quant_decode_irreversible_spqcd(spqcd, 8u, NULL) == DIC_STATUS_INVALID_ARGUMENT);

    return 0;
}

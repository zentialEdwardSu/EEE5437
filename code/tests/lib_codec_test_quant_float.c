#include <math.h>
#include <stdint.h>

#include "codec/quant.h"
#include "test_helpers.h"

int main(void) {
    int32_t half_step[] = {-5, -2, -1, 0, 1, 2, 5};
    const int32_t half_quantized[] = {-10, -4, -2, 0, 2, 4, 10};
    int32_t fractional_step[] = {-5, -2, -1, 0, 1, 2, 5};
    const int32_t fractional_quantized[] = {-2, -1, 0, 0, 0, 1, 2};
    const int32_t fractional_dequantized[] = {-5, -3, 0, 0, 0, 3, 5};
    size_t i;

    DIC_EXPECT(codec_quant_scalar_i32(half_step, 7u, 0.5f) ==
               DIC_STATUS_OK);
    for (i = 0u; i < 7u; ++i)
        DIC_EXPECT(half_step[i] == half_quantized[i]);

    DIC_EXPECT(codec_dequant_scalar_i32(half_step, 7u, 0.5f) ==
               DIC_STATUS_OK);
    DIC_EXPECT(half_step[0] == -5);
    DIC_EXPECT(half_step[6] == 5);

    DIC_EXPECT(codec_quant_scalar_i32(fractional_step, 7u, 2.5f) ==
               DIC_STATUS_OK);
    for (i = 0u; i < 7u; ++i)
        DIC_EXPECT(fractional_step[i] == fractional_quantized[i]);

    DIC_EXPECT(codec_dequant_scalar_i32(fractional_step, 7u, 2.5f) ==
               DIC_STATUS_OK);
    for (i = 0u; i < 7u; ++i)
        DIC_EXPECT(fractional_step[i] == fractional_dequantized[i]);

    DIC_EXPECT(codec_quant_scalar_i32(fractional_step, 7u, 0.0f) ==
               DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(codec_quant_scalar_i32(fractional_step, 7u, NAN) ==
               DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(codec_dequant_scalar_i32(fractional_step, 7u, INFINITY) ==
               DIC_STATUS_INVALID_ARGUMENT);
    return 0;
}

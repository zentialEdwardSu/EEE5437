#pragma once

#include <stdint.h>

#include "hw4/hw4_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

int dic_hw4_low_band_size(int length);
int dic_hw4_high_band_size(int length);
int dic_hw4_quant_step_for_level(int quality, int levels, int current_level);

dic_status dic_hw4_forward_transform_plane(int32_t *plane, int width, int height, int levels);
dic_status dic_hw4_inverse_transform_plane(int32_t *plane, int width, int height, int levels);

dic_status dic_hw4_quantize_plane(int32_t *plane, int width, int height, int levels, int quality);
dic_status dic_hw4_dequantize_plane(int32_t *plane, int width, int height, int levels, int quality);

#ifdef __cplusplus
}
#endif

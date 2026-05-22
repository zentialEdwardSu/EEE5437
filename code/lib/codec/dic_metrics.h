#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

double dic_metric_mse_u8(const uint8_t *a, const uint8_t *b, size_t sample_count);
double dic_metric_psnr_u8(const uint8_t *a, const uint8_t *b, size_t sample_count);
double dic_metric_bitrate(size_t encoded_bits, int width, int height);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

int dic_dwt53_low_size(int length);
int dic_dwt53_high_size(int length);

dic_status dic_dwt53_forward_plane(
    int32_t *plane,
    int width,
    int height,
    int levels
);

dic_status dic_dwt53_inverse_plane(
    int32_t *plane,
    int width,
    int height,
    int levels
);

dic_status dic_dwt53_validate_levels(int width, int height, int levels);

#ifdef __cplusplus
}
#endif

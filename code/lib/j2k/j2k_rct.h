#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

dic_status j2k_rct_forward(
    int32_t *samples,
    size_t pixel_count
);

dic_status j2k_rct_inverse(
    int32_t *samples,
    size_t pixel_count
);

#ifdef __cplusplus
}
#endif

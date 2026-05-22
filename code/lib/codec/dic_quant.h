#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

dic_status dic_quant_scalar_i32(
    int32_t *values,
    size_t count,
    int step_size
);

dic_status dic_dequant_scalar_i32(
    int32_t *values,
    size_t count,
    int step_size
);

#ifdef __cplusplus
}
#endif

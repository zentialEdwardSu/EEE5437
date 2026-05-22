#pragma once

#include <stdint.h>

#include "codec/dic_subband.h"

#ifdef __cplusplus
extern "C" {
#endif

dic_status dic_predict_ll_left(
    int32_t *plane,
    int stride,
    dic_rect_i32 rect
);

dic_status dic_unpredict_ll_left(
    int32_t *plane,
    int stride,
    dic_rect_i32 rect
);

#ifdef __cplusplus
}
#endif

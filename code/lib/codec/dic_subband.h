#pragma once

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dic_subband_orientation
{
    DIC_SUBBAND_LL = 0,
    DIC_SUBBAND_HL = 1,
    DIC_SUBBAND_LH = 2,
    DIC_SUBBAND_HH = 3
} dic_subband_orientation;

typedef struct dic_rect_i32
{
    int x;
    int y;
    int width;
    int height;
} dic_rect_i32;

dic_status dic_subband_lowest_ll_rect(
    int width,
    int height,
    int levels,
    dic_rect_i32 *rect
);

dic_status dic_subband_rect(
    int width,
    int height,
    int levels,
    int level,
    dic_subband_orientation orientation,
    dic_rect_i32 *rect
);

#ifdef __cplusplus
}
#endif

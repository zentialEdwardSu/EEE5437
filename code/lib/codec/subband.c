/**
 * @file subband.c
 * @brief Implements packed 5/3 DWT subband rectangle calculations.
 */

#include "codec/subband.h"

#include "wavelet/dic_dwt53.h"

dic_status codec_subband_lowest_ll_rect(
    int width,
    int height,
    int levels,
    dic_rect_i32 *rect
)
{
    int level;
    dic_status status;

    if (rect == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK)
        return status;

    for (level = 0; level < levels; ++level)
    {
        width = dic_dwt53_low_size(width);
        height = dic_dwt53_low_size(height);
    }

    rect->x = 0;
    rect->y = 0;
    rect->width = width;
    rect->height = height;
    return DIC_STATUS_OK;
}

dic_status codec_subband_rect(
    int width,
    int height,
    int levels,
    int level,
    codec_subband_orientation orientation,
    dic_rect_i32 *rect
)
{
    int i;
    int current_width;
    int current_height;
    int low_width;
    int low_height;
    int high_width;
    int high_height;
    dic_status status;

    if (rect == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    if (orientation == DIC_SUBBAND_LL)
        return codec_subband_lowest_ll_rect(width, height, levels, rect);

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK)
        return status;

    current_width = width;
    current_height = height;
    for (i = 1; i < level; ++i)
    {
        current_width = dic_dwt53_low_size(current_width);
        current_height = dic_dwt53_low_size(current_height);
    }

    low_width = dic_dwt53_low_size(current_width);
    low_height = dic_dwt53_low_size(current_height);
    high_width = dic_dwt53_high_size(current_width);
    high_height = dic_dwt53_high_size(current_height);

    if (orientation == DIC_SUBBAND_HL)
    {
        rect->x = low_width;
        rect->y = 0;
        rect->width = high_width;
        rect->height = low_height;
    }
    else if (orientation == DIC_SUBBAND_LH)
    {
        rect->x = 0;
        rect->y = low_height;
        rect->width = low_width;
        rect->height = high_height;
    }
    else if (orientation == DIC_SUBBAND_HH)
    {
        rect->x = low_width;
        rect->y = low_height;
        rect->width = high_width;
        rect->height = high_height;
    }
    else
    {
        return DIC_STATUS_INVALID_ARGUMENT;
    }

    return DIC_STATUS_OK;
}

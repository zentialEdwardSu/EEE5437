#include "codec/predict.h"

#include <stddef.h>

dic_status codec_predict_ll_left(int32_t *plane, int stride, dic_rect_i32 rect)
{
    int y;

    if (plane == NULL || stride <= 0 || rect.width <= 0 || rect.height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (y = 0; y < rect.height; ++y)
    {
        int x;
        int32_t previous = 0;
        int32_t *row = plane + ((size_t)(rect.y + y) * (size_t)stride) + (size_t)rect.x;

        for (x = 0; x < rect.width; ++x)
        {
            int32_t original = row[x];
            row[x] = original - previous;
            previous = original;
        }
    }

    return DIC_STATUS_OK;
}

dic_status codec_unpredict_ll_left(int32_t *plane, int stride, dic_rect_i32 rect)
{
    int y;

    if (plane == NULL || stride <= 0 || rect.width <= 0 || rect.height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (y = 0; y < rect.height; ++y)
    {
        int x;
        int32_t previous = 0;
        int32_t *row = plane + ((size_t)(rect.y + y) * (size_t)stride) + (size_t)rect.x;

        for (x = 0; x < rect.width; ++x)
        {
            row[x] += previous;
            previous = row[x];
        }
    }

    return DIC_STATUS_OK;
}

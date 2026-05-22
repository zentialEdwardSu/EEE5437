#include "wavelet/dic_dwt53.h"

#include <stddef.h>
#include <stdlib.h>

int dic_dwt53_low_size(int length)
{
    return (length + 1) / 2;
}

int dic_dwt53_high_size(int length)
{
    return length / 2;
}

dic_status dic_dwt53_validate_levels(int width, int height, int levels)
{
    int level;

    if (width <= 0 || height <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;
    if (levels <= 0)
        return DIC_HW4_INVALID_LEVELS;

    for (level = 0; level < levels; ++level)
    {
        if (width < 2 || height < 2)
            return DIC_HW4_INVALID_LEVELS;

        width = dic_dwt53_low_size(width);
        height = dic_dwt53_low_size(height);
    }

    return DIC_STATUS_OK;
}

static int32_t dic_dwt53_floor_div2(int32_t value)
{
    if (value >= 0)
        return value / 2;
    return -(((-value) + 1) / 2);
}

static int32_t dic_dwt53_floor_div4(int32_t value)
{
    if (value >= 0)
        return value / 4;
    return -(((-value) + 3) / 4);
}

static dic_status dic_dwt53_forward_1d(int32_t *samples, int length, int32_t *scratch)
{
    int low_count;
    int high_count;
    int32_t *low;
    int32_t *high;
    int i;

    if (samples == NULL || scratch == NULL || length <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (length == 1)
        return DIC_STATUS_OK;

    low_count = dic_dwt53_low_size(length);
    high_count = dic_dwt53_high_size(length);
    low = scratch;
    high = scratch + low_count;

    for (i = 0; i < low_count; ++i)
        low[i] = samples[i * 2];
    for (i = 0; i < high_count; ++i)
        high[i] = samples[(i * 2) + 1];

    for (i = 0; i < high_count; ++i)
    {
        int32_t left = low[i];
        int32_t right = low[(i + 1) < low_count ? (i + 1) : (low_count - 1)];
        high[i] -= dic_dwt53_floor_div2(left + right);
    }

    for (i = 0; i < low_count; ++i)
    {
        int32_t left;
        int32_t right;

        if (high_count == 0)
            break;

        left = high[i > 0 ? (i - 1) : 0];
        right = high[i < high_count ? i : (high_count - 1)];
        low[i] += dic_dwt53_floor_div4(left + right + 2);
    }

    for (i = 0; i < low_count; ++i)
        samples[i] = low[i];
    for (i = 0; i < high_count; ++i)
        samples[low_count + i] = high[i];

    return DIC_STATUS_OK;
}

static dic_status dic_dwt53_inverse_1d(int32_t *samples, int length, int32_t *scratch)
{
    int low_count;
    int high_count;
    int32_t *low;
    int32_t *high;
    int i;

    if (samples == NULL || scratch == NULL || length <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (length == 1)
        return DIC_STATUS_OK;

    low_count = dic_dwt53_low_size(length);
    high_count = dic_dwt53_high_size(length);
    low = scratch;
    high = scratch + low_count;

    for (i = 0; i < low_count; ++i)
        low[i] = samples[i];
    for (i = 0; i < high_count; ++i)
        high[i] = samples[low_count + i];

    for (i = 0; i < low_count; ++i)
    {
        int32_t left;
        int32_t right;

        if (high_count == 0)
            break;

        left = high[i > 0 ? (i - 1) : 0];
        right = high[i < high_count ? i : (high_count - 1)];
        low[i] -= dic_dwt53_floor_div4(left + right + 2);
    }

    for (i = 0; i < high_count; ++i)
    {
        int32_t left = low[i];
        int32_t right = low[(i + 1) < low_count ? (i + 1) : (low_count - 1)];
        high[i] += dic_dwt53_floor_div2(left + right);
    }

    for (i = 0; i < high_count; ++i)
    {
        samples[i * 2] = low[i];
        samples[(i * 2) + 1] = high[i];
    }
    if (low_count > high_count)
        samples[length - 1] = low[low_count - 1];

    return DIC_STATUS_OK;
}

static dic_status dic_dwt53_transform_rows(
    int32_t *plane,
    int stride,
    int width,
    int height,
    int inverse,
    int32_t *scratch
)
{
    int y;

    for (y = 0; y < height; ++y)
    {
        int32_t *row = plane + ((size_t)y * (size_t)stride);
        dic_status status = inverse
            ? dic_dwt53_inverse_1d(row, width, scratch)
            : dic_dwt53_forward_1d(row, width, scratch);
        if (status != DIC_STATUS_OK)
            return status;
    }

    return DIC_STATUS_OK;
}

static dic_status dic_dwt53_transform_columns(
    int32_t *plane,
    int stride,
    int width,
    int height,
    int inverse,
    int32_t *scratch
)
{
    int x;
    int y;

    for (x = 0; x < width; ++x)
    {
        dic_status status;

        for (y = 0; y < height; ++y)
            scratch[y] = plane[((size_t)y * (size_t)stride) + (size_t)x];

        status = inverse
            ? dic_dwt53_inverse_1d(scratch, height, scratch + height)
            : dic_dwt53_forward_1d(scratch, height, scratch + height);
        if (status != DIC_STATUS_OK)
            return status;

        for (y = 0; y < height; ++y)
            plane[((size_t)y * (size_t)stride) + (size_t)x] = scratch[y];
    }

    return DIC_STATUS_OK;
}

static dic_status dic_dwt53_transform_plane(
    int32_t *plane,
    int width,
    int height,
    int levels,
    int inverse
)
{
    int max_dimension;
    int32_t *scratch = NULL;
    dic_status status;
    int level;
    int current_width;
    int current_height;

    if (plane == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK)
        return status;

    max_dimension = width > height ? width : height;
    scratch = (int32_t *)malloc((size_t)max_dimension * 2u * sizeof(int32_t));
    if (scratch == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    if (!inverse)
    {
        current_width = width;
        current_height = height;
        status = DIC_STATUS_OK;
        for (level = 1; level <= levels; ++level)
        {
            status = dic_dwt53_transform_columns(
                plane,
                width,
                current_width,
                current_height,
                0,
                scratch
            );
            if (status != DIC_STATUS_OK)
                break;

            status = dic_dwt53_transform_rows(
                plane,
                width,
                current_width,
                current_height,
                0,
                scratch
            );
            if (status != DIC_STATUS_OK)
                break;

            current_width = dic_dwt53_low_size(current_width);
            current_height = dic_dwt53_low_size(current_height);
        }
    }
    else
    {
        int *widths = (int *)malloc((size_t)levels * sizeof(int));
        int *heights = (int *)malloc((size_t)levels * sizeof(int));
        if (widths == NULL || heights == NULL)
        {
            free(widths);
            free(heights);
            free(scratch);
            return DIC_STATUS_MEMORY_ERROR;
        }

        current_width = width;
        current_height = height;
        for (level = 0; level < levels; ++level)
        {
            widths[level] = current_width;
            heights[level] = current_height;
            current_width = dic_dwt53_low_size(current_width);
            current_height = dic_dwt53_low_size(current_height);
        }

        status = DIC_STATUS_OK;
        for (level = levels - 1; level >= 0; --level)
        {
            status = dic_dwt53_transform_rows(
                plane,
                width,
                widths[level],
                heights[level],
                1,
                scratch
            );
            if (status != DIC_STATUS_OK)
                break;

            status = dic_dwt53_transform_columns(
                plane,
                width,
                widths[level],
                heights[level],
                1,
                scratch
            );
            if (status != DIC_STATUS_OK)
                break;
        }

        free(widths);
        free(heights);
    }

    free(scratch);
    return status;
}

dic_status dic_dwt53_forward_plane(
    int32_t *plane,
    int width,
    int height,
    int levels
)
{
    return dic_dwt53_transform_plane(plane, width, height, levels, 0);
}

dic_status dic_dwt53_inverse_plane(
    int32_t *plane,
    int width,
    int height,
    int levels
)
{
    return dic_dwt53_transform_plane(plane, width, height, levels, 1);
}

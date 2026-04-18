#include "hw4/hw4_jpeg2000.h"

#include <stddef.h>
#include <stdlib.h>

int dic_hw4_low_band_size(int length)
{
    return (length + 1) / 2;
}

int dic_hw4_high_band_size(int length)
{
    return length / 2;
}

int dic_hw4_quant_step_for_level(int quality, int levels, int current_level)
{
    int base_step;

    if (quality < 1)
        quality = 1;
    if (quality > 100)
        quality = 100;
    if (levels < 1)
        levels = 1;
    if (current_level < 1)
        current_level = 1;
    if (current_level > levels)
        current_level = levels;

    base_step = 1 + ((100 - quality) * 31) / 99;
    return base_step * (levels - current_level + 1);
}

static int32_t dic_hw4_div_floor2(int32_t value)
{
    if (value >= 0)
        return value / 2;
    return -(((-value) + 1) / 2);
}

static int32_t dic_hw4_div_floor4(int32_t value)
{
    if (value >= 0)
        return value / 4;
    return -(((-value) + 3) / 4);
}

static int32_t dic_hw4_div_round_nearest(int32_t value, int step)
{
    int32_t positive_step = (int32_t)step;

    if (step <= 1)
        return value;
    if (value >= 0)
        return (value + (positive_step / 2)) / positive_step;
    return -(((-value) + (positive_step / 2)) / positive_step);
}

static dic_status dic_hw4_forward_transform_1d(
    int32_t *samples,
    int length,
    int32_t *scratch
)
{
    int low_count;
    int high_count;
    int i;
    int32_t *low;
    int32_t *high;

    if (samples == NULL || scratch == NULL || length <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (length == 1)
        return DIC_STATUS_OK;

    low_count = dic_hw4_low_band_size(length);
    high_count = dic_hw4_high_band_size(length);
    low = scratch;
    high = scratch + low_count;

    for (i = 0; i < low_count; ++i)
        low[i] = samples[i * 2];
    for (i = 0; i < high_count; ++i)
        high[i] = samples[(i * 2) + 1];

    /*
     * JPEG2000's reversible 5/3 transform uses symmetric extension at the
     * borders. Without that mirrored access, odd image sizes and edge samples
     * would need ad-hoc padding and the inverse transform would no longer
     * follow the same integer lifting steps.
     */
    for (i = 0; i < high_count; ++i)
    {
        int32_t left = low[i];
        int32_t right = low[(i + 1) < low_count ? (i + 1) : (low_count - 1)];
        high[i] -= dic_hw4_div_floor2(left + right);
    }

    for (i = 0; i < low_count; ++i)
    {
        int32_t left;
        int32_t right;

        if (high_count == 0)
            break;

        left = high[i > 0 ? (i - 1) : 0];
        right = high[i < high_count ? i : (high_count - 1)];
        low[i] += dic_hw4_div_floor4(left + right + 2);
    }

    for (i = 0; i < low_count; ++i)
        samples[i] = low[i];
    for (i = 0; i < high_count; ++i)
        samples[low_count + i] = high[i];

    return DIC_STATUS_OK;
}

static dic_status dic_hw4_inverse_transform_1d(
    int32_t *samples,
    int length,
    int32_t *scratch
)
{
    int low_count;
    int high_count;
    int i;
    int32_t *low;
    int32_t *high;

    if (samples == NULL || scratch == NULL || length <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (length == 1)
        return DIC_STATUS_OK;

    low_count = dic_hw4_low_band_size(length);
    high_count = dic_hw4_high_band_size(length);
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
        low[i] -= dic_hw4_div_floor4(left + right + 2);
    }

    for (i = 0; i < high_count; ++i)
    {
        int32_t left = low[i];
        int32_t right = low[(i + 1) < low_count ? (i + 1) : (low_count - 1)];
        high[i] += dic_hw4_div_floor2(left + right);
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

static dic_status dic_hw4_transform_rows(
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
            ? dic_hw4_inverse_transform_1d(row, width, scratch)
            : dic_hw4_forward_transform_1d(row, width, scratch);
        if (status != DIC_STATUS_OK)
            return status;
    }

    return DIC_STATUS_OK;
}

static dic_status dic_hw4_transform_columns(
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
            ? dic_hw4_inverse_transform_1d(scratch, height, scratch + height)
            : dic_hw4_forward_transform_1d(scratch, height, scratch + height);
        if (status != DIC_STATUS_OK)
            return status;

        for (y = 0; y < height; ++y)
            plane[((size_t)y * (size_t)stride) + (size_t)x] = scratch[y];
    }

    return DIC_STATUS_OK;
}

static dic_status dic_hw4_transform_plane_impl(
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

    status = dic_hw4_validate_codec_params(width, height, 1, levels, 100);
    if (status != DIC_STATUS_OK)
        return status;

    max_dimension = width > height ? width : height;
    scratch = (int32_t *)malloc((size_t)(max_dimension * 2) * sizeof(int32_t));
    if (scratch == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    if (!inverse)
    {
        current_width = width;
        current_height = height;
        for (level = 1; level <= levels; ++level)
        {
            status = dic_hw4_transform_rows(
                plane,
                width,
                current_width,
                current_height,
                0,
                scratch
            );
            if (status != DIC_STATUS_OK)
                break;

            status = dic_hw4_transform_columns(
                plane,
                width,
                current_width,
                current_height,
                0,
                scratch
            );
            if (status != DIC_STATUS_OK)
                break;

            current_width = dic_hw4_low_band_size(current_width);
            current_height = dic_hw4_low_band_size(current_height);
        }
    }
    else
    {
        int *widths = NULL;
        int *heights = NULL;

        widths = (int *)malloc((size_t)levels * sizeof(int));
        heights = (int *)malloc((size_t)levels * sizeof(int));
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
            current_width = dic_hw4_low_band_size(current_width);
            current_height = dic_hw4_low_band_size(current_height);
        }

        status = DIC_STATUS_OK;
        for (level = levels - 1; level >= 0; --level)
        {
            status = dic_hw4_transform_columns(
                plane,
                width,
                widths[level],
                heights[level],
                1,
                scratch
            );
            if (status != DIC_STATUS_OK)
                break;

            status = dic_hw4_transform_rows(
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

dic_status dic_hw4_forward_transform_plane(int32_t *plane, int width, int height, int levels)
{
    return dic_hw4_transform_plane_impl(plane, width, height, levels, 0);
}

dic_status dic_hw4_inverse_transform_plane(int32_t *plane, int width, int height, int levels)
{
    return dic_hw4_transform_plane_impl(plane, width, height, levels, 1);
}

static void dic_hw4_apply_scalar_to_band(
    int32_t *plane,
    int stride,
    int x0,
    int y0,
    int band_width,
    int band_height,
    int step,
    int inverse
)
{
    int y;
    int x;

    for (y = 0; y < band_height; ++y)
    {
        for (x = 0; x < band_width; ++x)
        {
            int32_t *sample = plane
                + ((size_t)(y0 + y) * (size_t)stride)
                + (size_t)(x0 + x);
            if (!inverse)
                *sample = dic_hw4_div_round_nearest(*sample, step);
            else
                *sample *= (int32_t)step;
        }
    }
}

static dic_status dic_hw4_quantize_impl(
    int32_t *plane,
    int width,
    int height,
    int levels,
    int quality,
    int inverse
)
{
    int current_width = width;
    int current_height = height;
    int level;

    if (plane == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    if (dic_hw4_validate_codec_params(width, height, 1, levels, quality) != DIC_STATUS_OK)
        return dic_hw4_validate_codec_params(width, height, 1, levels, quality);

    for (level = 1; level <= levels; ++level)
    {
        int low_width = dic_hw4_low_band_size(current_width);
        int low_height = dic_hw4_low_band_size(current_height);
        int high_width = dic_hw4_high_band_size(current_width);
        int high_height = dic_hw4_high_band_size(current_height);
        int step = dic_hw4_quant_step_for_level(quality, levels, level);

        dic_hw4_apply_scalar_to_band(
            plane,
            width,
            low_width,
            0,
            high_width,
            low_height,
            step,
            inverse
        );
        dic_hw4_apply_scalar_to_band(
            plane,
            width,
            0,
            low_height,
            low_width,
            high_height,
            step,
            inverse
        );
        dic_hw4_apply_scalar_to_band(
            plane,
            width,
            low_width,
            low_height,
            high_width,
            high_height,
            step,
            inverse
        );

        current_width = low_width;
        current_height = low_height;
    }

    return DIC_STATUS_OK;
}

dic_status dic_hw4_quantize_plane(int32_t *plane, int width, int height, int levels, int quality)
{
    return dic_hw4_quantize_impl(plane, width, height, levels, quality, 0);
}

dic_status dic_hw4_dequantize_plane(int32_t *plane, int width, int height, int levels, int quality)
{
    return dic_hw4_quantize_impl(plane, width, height, levels, quality, 1);
}

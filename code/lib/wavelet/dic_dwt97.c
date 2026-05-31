/**
 * Irreversible JPEG 2000 9/7 wavelet transform for double image planes.
 *
 * The implementation follows the lifting factorization with symmetric edge
 * extension and stores each one-dimensional result as low-pass samples
 * followed by high-pass samples, matching the reversible 5/3 module layout.
 */
#include "wavelet/dic_dwt97.h"

#include <stddef.h>
#include <stdlib.h>

enum
{
    DIC_DWT97_FORWARD = 0,
    DIC_DWT97_INVERSE = 1
};

static const double DIC_DWT97_ALPHA = -1.586134342059924;
static const double DIC_DWT97_BETA = -0.052980118572961;
static const double DIC_DWT97_GAMMA = 0.882911075530934;
static const double DIC_DWT97_DELTA = 0.443506852043971;
static const double DIC_DWT97_K = 1.230174104914001;

int dic_dwt97_low_size(int length)
{
    return (length + 1) / 2;
}

int dic_dwt97_high_size(int length)
{
    return length / 2;
}

dic_status dic_dwt97_validate_levels(int width, int height, int levels)
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

        width = dic_dwt97_low_size(width);
        height = dic_dwt97_low_size(height);
    }

    return DIC_STATUS_OK;
}

static double dic_dwt97_left_high(const double *high, int index)
{
    return high[index > 0 ? (index - 1) : 0];
}

static double dic_dwt97_right_high(const double *high, int high_count, int index)
{
    return high[index < high_count ? index : (high_count - 1)];
}

static double dic_dwt97_right_low(const double *low, int low_count, int index)
{
    return low[(index + 1) < low_count ? (index + 1) : (low_count - 1)];
}

static void dic_dwt97_update_high(
    double *high,
    const double *low,
    int low_count,
    int high_count,
    double coefficient
)
{
    int i;

    for (i = 0; i < high_count; ++i)
        high[i] += coefficient * (low[i] + dic_dwt97_right_low(low, low_count, i));
}

static void dic_dwt97_update_low(
    double *low,
    const double *high,
    int low_count,
    int high_count,
    double coefficient
)
{
    int i;

    if (high_count == 0)
        return;

    for (i = 0; i < low_count; ++i)
    {
        low[i] += coefficient * (
            dic_dwt97_left_high(high, i) +
            dic_dwt97_right_high(high, high_count, i)
        );
    }
}

static dic_status dic_dwt97_forward_1d(double *samples, int length, double *scratch)
{
    int low_count;
    int high_count;
    double *low;
    double *high;
    int i;

    if (samples == NULL || scratch == NULL || length <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (length == 1)
        return DIC_STATUS_OK;

    low_count = dic_dwt97_low_size(length);
    high_count = dic_dwt97_high_size(length);
    low = scratch;
    high = scratch + low_count;

    for (i = 0; i < low_count; ++i)
        low[i] = samples[i * 2];
    for (i = 0; i < high_count; ++i)
        high[i] = samples[(i * 2) + 1];

    dic_dwt97_update_high(high, low, low_count, high_count, DIC_DWT97_ALPHA);
    dic_dwt97_update_low(low, high, low_count, high_count, DIC_DWT97_BETA);
    dic_dwt97_update_high(high, low, low_count, high_count, DIC_DWT97_GAMMA);
    dic_dwt97_update_low(low, high, low_count, high_count, DIC_DWT97_DELTA);

    for (i = 0; i < low_count; ++i)
        samples[i] = low[i] * DIC_DWT97_K;
    for (i = 0; i < high_count; ++i)
        samples[low_count + i] = high[i] / DIC_DWT97_K;

    return DIC_STATUS_OK;
}

static dic_status dic_dwt97_inverse_1d(double *samples, int length, double *scratch)
{
    int low_count;
    int high_count;
    double *low;
    double *high;
    int i;

    if (samples == NULL || scratch == NULL || length <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (length == 1)
        return DIC_STATUS_OK;

    low_count = dic_dwt97_low_size(length);
    high_count = dic_dwt97_high_size(length);
    low = scratch;
    high = scratch + low_count;

    for (i = 0; i < low_count; ++i)
        low[i] = samples[i] / DIC_DWT97_K;
    for (i = 0; i < high_count; ++i)
        high[i] = samples[low_count + i] * DIC_DWT97_K;

    dic_dwt97_update_low(low, high, low_count, high_count, -DIC_DWT97_DELTA);
    dic_dwt97_update_high(high, low, low_count, high_count, -DIC_DWT97_GAMMA);
    dic_dwt97_update_low(low, high, low_count, high_count, -DIC_DWT97_BETA);
    dic_dwt97_update_high(high, low, low_count, high_count, -DIC_DWT97_ALPHA);

    for (i = 0; i < high_count; ++i)
    {
        samples[i * 2] = low[i];
        samples[(i * 2) + 1] = high[i];
    }
    if (low_count > high_count)
        samples[length - 1] = low[low_count - 1];

    return DIC_STATUS_OK;
}

static dic_status dic_dwt97_transform_rows(
    double *plane,
    int stride,
    int width,
    int height,
    int inverse,
    double *scratch
)
{
    int y;

    for (y = 0; y < height; ++y)
    {
        double *row = plane + ((size_t)y * (size_t)stride);
        dic_status status = inverse
            ? dic_dwt97_inverse_1d(row, width, scratch)
            : dic_dwt97_forward_1d(row, width, scratch);
        if (status != DIC_STATUS_OK)
            return status;
    }

    return DIC_STATUS_OK;
}

static dic_status dic_dwt97_transform_columns(
    double *plane,
    int stride,
    int width,
    int height,
    int inverse,
    double *scratch
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
            ? dic_dwt97_inverse_1d(scratch, height, scratch + height)
            : dic_dwt97_forward_1d(scratch, height, scratch + height);
        if (status != DIC_STATUS_OK)
            return status;

        for (y = 0; y < height; ++y)
            plane[((size_t)y * (size_t)stride) + (size_t)x] = scratch[y];
    }

    return DIC_STATUS_OK;
}

static dic_status dic_dwt97_transform_forward(
    double *plane,
    int width,
    int height,
    int levels,
    double *scratch
)
{
    int level;
    int current_width = width;
    int current_height = height;

    for (level = 1; level <= levels; ++level)
    {
        dic_status status = dic_dwt97_transform_columns(
            plane,
            width,
            current_width,
            current_height,
            DIC_DWT97_FORWARD,
            scratch
        );
        if (status != DIC_STATUS_OK)
            return status;

        status = dic_dwt97_transform_rows(
            plane,
            width,
            current_width,
            current_height,
            DIC_DWT97_FORWARD,
            scratch
        );
        if (status != DIC_STATUS_OK)
            return status;

        current_width = dic_dwt97_low_size(current_width);
        current_height = dic_dwt97_low_size(current_height);
    }

    return DIC_STATUS_OK;
}

static dic_status dic_dwt97_transform_inverse(
    double *plane,
    int width,
    int height,
    int levels,
    double *scratch
)
{
    int level;
    int current_width = width;
    int current_height = height;
    int *widths = (int *)malloc((size_t)levels * sizeof(int));
    int *heights = (int *)malloc((size_t)levels * sizeof(int));
    dic_status status = DIC_STATUS_OK;

    if (widths == NULL || heights == NULL)
    {
        free(widths);
        free(heights);
        return DIC_STATUS_MEMORY_ERROR;
    }

    for (level = 0; level < levels; ++level)
    {
        widths[level] = current_width;
        heights[level] = current_height;
        current_width = dic_dwt97_low_size(current_width);
        current_height = dic_dwt97_low_size(current_height);
    }

    for (level = levels - 1; level >= 0; --level)
    {
        status = dic_dwt97_transform_rows(
            plane,
            width,
            widths[level],
            heights[level],
            DIC_DWT97_INVERSE,
            scratch
        );
        if (status != DIC_STATUS_OK)
            break;

        status = dic_dwt97_transform_columns(
            plane,
            width,
            widths[level],
            heights[level],
            DIC_DWT97_INVERSE,
            scratch
        );
        if (status != DIC_STATUS_OK)
            break;
    }

    free(widths);
    free(heights);
    return status;
}

static dic_status dic_dwt97_transform_plane(
    double *plane,
    int width,
    int height,
    int levels,
    int inverse
)
{
    int max_dimension;
    double *scratch;
    dic_status status;

    if (plane == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_dwt97_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK)
        return status;

    max_dimension = width > height ? width : height;
    scratch = (double *)malloc((size_t)max_dimension * 2u * sizeof(double));
    if (scratch == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    status = inverse
        ? dic_dwt97_transform_inverse(plane, width, height, levels, scratch)
        : dic_dwt97_transform_forward(plane, width, height, levels, scratch);

    free(scratch);
    return status;
}

dic_status dic_dwt97_forward_plane(
    double *plane,
    int width,
    int height,
    int levels
)
{
    return dic_dwt97_transform_plane(plane, width, height, levels, DIC_DWT97_FORWARD);
}

dic_status dic_dwt97_inverse_plane(
    double *plane,
    int width,
    int height,
    int levels
)
{
    return dic_dwt97_transform_plane(plane, width, height, levels, DIC_DWT97_INVERSE);
}

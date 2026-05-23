/**
 * @file dic_j2k_roi.c
 * @brief Builds JPEG 2000 Maxshift ROI coefficient maps for reversible 5-3 transformed tiles.
 *
 * The module traces an image-domain rectangular ROI backwards through the inverse 5-3
 * synthesis dependencies from T.800 Annex H.3.1.1. The resulting coefficient-domain map is
 * then used by the image encoder to shift ROI-mask coefficients before EBCOT coding, so the
 * ROI participates in packet bit-plane ordering instead of only being signalled by RGN.
 */

#include "j2k/dic_j2k_roi.h"

#include <stdlib.h>
#include <string.h>

#include "wavelet/dic_dwt53.h"

static void dic_j2k_roi_mark_low_dependency(
    uint8_t *input,
    int length,
    int low_index
)
{
    int low_count = dic_dwt53_low_size(length);
    int high_count = dic_dwt53_high_size(length);

    if (low_index < 0)
        low_index = 0;
    if (low_index >= low_count)
        low_index = low_count - 1;
    input[low_index] = 1u;
    if (high_count > 0)
    {
        int left_high = low_index > 0 ? low_index - 1 : 0;
        int right_high = low_index < high_count ? low_index : high_count - 1;

        input[low_count + left_high] = 1u;
        input[low_count + right_high] = 1u;
    }
}

/* Reference: paper/T-REC-T.800-200208.pdf, H.3.1.1, inverse 5-3 samples depend on neighboring low/high coefficients. */
static void dic_j2k_roi_trace_inverse_1d(
    const uint8_t *output,
    int length,
    uint8_t *input
)
{
    int index;
    int low_count = dic_dwt53_low_size(length);

    memset(input, 0, (size_t)length);
    if (length <= 1)
    {
        if (length == 1 && output[0] != 0u)
            input[0] = 1u;
        return;
    }

    for (index = 0; index < length; ++index)
    {
        int n;

        if (output[index] == 0u)
            continue;
        n = index / 2;
        if ((index & 1) == 0)
        {
            dic_j2k_roi_mark_low_dependency(input, length, n);
        }
        else
        {
            int high_count = dic_dwt53_high_size(length);

            if (n < high_count)
                input[low_count + n] = 1u;
            dic_j2k_roi_mark_low_dependency(input, length, n);
            dic_j2k_roi_mark_low_dependency(input, length, n + 1);
        }
    }
}

static dic_status dic_j2k_roi_trace_columns(
    const uint8_t *source,
    int stride,
    int width,
    int height,
    uint8_t *destination,
    uint8_t *line,
    uint8_t *traced
)
{
    int x;

    memset(destination, 0, (size_t)stride * (size_t)height);
    for (x = 0; x < width; ++x)
    {
        int y;

        for (y = 0; y < height; ++y)
            line[y] = source[(size_t)y * (size_t)stride + (size_t)x];
        dic_j2k_roi_trace_inverse_1d(line, height, traced);
        for (y = 0; y < height; ++y)
            destination[(size_t)y * (size_t)stride + (size_t)x] = traced[y];
    }
    return DIC_STATUS_OK;
}

static dic_status dic_j2k_roi_trace_rows(
    const uint8_t *source,
    int stride,
    int width,
    int height,
    uint8_t *destination,
    uint8_t *line,
    uint8_t *traced
)
{
    int y;

    memset(destination, 0, (size_t)stride * (size_t)height);
    for (y = 0; y < height; ++y)
    {
        int x;

        memcpy(line, source + (size_t)y * (size_t)stride, (size_t)width);
        dic_j2k_roi_trace_inverse_1d(line, width, traced);
        for (x = 0; x < width; ++x)
            destination[(size_t)y * (size_t)stride + (size_t)x] = traced[x];
    }
    return DIC_STATUS_OK;
}

static dic_status dic_j2k_roi_trace_one_level(
    const uint8_t *source,
    int stride,
    int width,
    int height,
    uint8_t *destination,
    uint8_t *scratch,
    uint8_t *line,
    uint8_t *traced
)
{
    dic_status status;

    status = dic_j2k_roi_trace_columns(source, stride, width, height, scratch, line, traced);
    if (status != DIC_STATUS_OK)
        return status;
    return dic_j2k_roi_trace_rows(scratch, stride, width, height, destination, line, traced);
}

static int dic_j2k_roi_rect_intersects_image(
    const dic_rect_i32 *rect,
    int width,
    int height
)
{
    if (rect == NULL || rect->width <= 0 || rect->height <= 0)
        return 0;
    if (rect->x >= width || rect->y >= height)
        return 0;
    if (rect->x + rect->width <= 0 || rect->y + rect->height <= 0)
        return 0;
    return 1;
}

static void dic_j2k_roi_seed_image_mask(
    uint8_t *mask,
    int width,
    int height,
    const dic_rect_i32 *roi_rect
)
{
    int y_start = roi_rect->y < 0 ? 0 : roi_rect->y;
    int y_end = roi_rect->y + roi_rect->height > height ? height : roi_rect->y + roi_rect->height;
    int x_start = roi_rect->x < 0 ? 0 : roi_rect->x;
    int x_end = roi_rect->x + roi_rect->width > width ? width : roi_rect->x + roi_rect->width;
    int y;

    for (y = y_start; y < y_end; ++y)
    {
        int x;

        for (x = x_start; x < x_end; ++x)
            mask[(size_t)y * (size_t)width + (size_t)x] = 1u;
    }
}

dic_status dic_j2k_roi_build_shift_map(
    int width,
    int height,
    int levels,
    const dic_rect_i32 *roi_rect,
    uint8_t **shift_map
)
{
    uint8_t *current = NULL;
    uint8_t *traced_level = NULL;
    uint8_t *scratch = NULL;
    uint8_t *line = NULL;
    uint8_t *traced = NULL;
    uint8_t *result = NULL;
    int max_dimension;
    int current_width;
    int current_height;
    int level;
    dic_status status = DIC_STATUS_OK;

    if (shift_map == NULL || width <= 0 || height <= 0 || levels < 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    *shift_map = NULL;
    if (!dic_j2k_roi_rect_intersects_image(roi_rect, width, height))
        return DIC_STATUS_OK;

    current = (uint8_t *)calloc((size_t)width * (size_t)height, sizeof(current[0]));
    traced_level = (uint8_t *)calloc((size_t)width * (size_t)height, sizeof(traced_level[0]));
    scratch = (uint8_t *)calloc((size_t)width * (size_t)height, sizeof(scratch[0]));
    result = (uint8_t *)calloc((size_t)width * (size_t)height, sizeof(result[0]));
    max_dimension = width > height ? width : height;
    line = (uint8_t *)malloc((size_t)max_dimension);
    traced = (uint8_t *)malloc((size_t)max_dimension);
    if (current == NULL || traced_level == NULL || scratch == NULL || result == NULL || line == NULL || traced == NULL)
    {
        status = DIC_STATUS_MEMORY_ERROR;
        goto cleanup;
    }

    dic_j2k_roi_seed_image_mask(current, width, height, roi_rect);
    current_width = width;
    current_height = height;
    for (level = 0; level < levels && status == DIC_STATUS_OK; ++level)
    {
        int y;
        int low_width;
        int low_height;

        status = dic_j2k_roi_trace_one_level(
            current,
            width,
            current_width,
            current_height,
            traced_level,
            scratch,
            line,
            traced
        );
        if (status != DIC_STATUS_OK)
            break;
        for (y = 0; y < current_height; ++y)
        {
            int x;

            for (x = 0; x < current_width; ++x)
            {
                if (traced_level[(size_t)y * (size_t)width + (size_t)x] != 0u)
                    result[(size_t)y * (size_t)width + (size_t)x] = 1u;
            }
        }

        low_width = dic_dwt53_low_size(current_width);
        low_height = dic_dwt53_low_size(current_height);
        memset(current, 0, (size_t)width * (size_t)height);
        for (y = 0; y < low_height; ++y)
        {
            memcpy(
                current + (size_t)y * (size_t)width,
                traced_level + (size_t)y * (size_t)width,
                (size_t)low_width
            );
        }
        current_width = low_width;
        current_height = low_height;
    }
    if (status == DIC_STATUS_OK && levels == 0)
        memcpy(result, current, (size_t)width * (size_t)height);

cleanup:
    free(current);
    free(traced_level);
    free(scratch);
    free(line);
    free(traced);
    if (status == DIC_STATUS_OK)
        *shift_map = result;
    else
        free(result);
    return status;
}

static uint32_t dic_j2k_roi_value_magnitude(int32_t value)
{
    return value < 0 ? (uint32_t)(-(value + 1)) + 1u : (uint32_t)value;
}

static dic_status dic_j2k_roi_shift_value(int32_t value, uint8_t shift, int32_t *shifted)
{
    uint32_t magnitude;
    uint32_t shifted_magnitude;

    if (shifted == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (value == 0 || shift == 0u)
    {
        *shifted = value;
        return DIC_STATUS_OK;
    }
    if (shift >= 31u)
        return DIC_STATUS_INVALID_ARGUMENT;

    magnitude = dic_j2k_roi_value_magnitude(value);
    if (magnitude > ((uint32_t)INT32_MAX >> shift))
        return DIC_STATUS_INVALID_ARGUMENT;

    shifted_magnitude = magnitude << shift;
    *shifted = value < 0 ? -((int32_t)shifted_magnitude) : (int32_t)shifted_magnitude;
    return DIC_STATUS_OK;
}

dic_status dic_j2k_roi_apply_shift_map(
    int32_t *plane,
    int width,
    int height,
    const uint8_t *shift_map,
    uint8_t shift
)
{
    size_t sample_count;
    size_t index;
    dic_status status;

    if (plane == NULL || width <= 0 || height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (shift_map == NULL || shift == 0u)
        return DIC_STATUS_OK;
    sample_count = (size_t)width * (size_t)height;
    for (index = 0u; index < sample_count; ++index)
    {
        if (shift_map[index] != 0u)
        {
            status = dic_j2k_roi_shift_value(plane[index], shift, plane + index);
            if (status != DIC_STATUS_OK)
                return status;
        }
    }
    return DIC_STATUS_OK;
}

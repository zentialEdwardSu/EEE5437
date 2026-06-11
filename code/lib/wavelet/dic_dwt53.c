#include "wavelet/dic_dwt53.h"

#include <stddef.h>
#include <stdlib.h>

inline int dic_dwt53_low_size(int length) { return (length + 1) / 2; }

inline int dic_dwt53_high_size(int length) { return length / 2; }

dic_status dic_dwt53_validate_levels(int width, int height, int levels) {
    if (width <= 0 || height <= 0) return DIC_HW4_INVALID_DIMENSIONS;
    if (levels < 0) return DIC_HW4_INVALID_LEVELS;

    if (levels == 0) return DIC_STATUS_OK;

    for (int level = 0; level < levels; level++) {
        if (width < 2 || height < 2) return DIC_HW4_INVALID_LEVELS;

        width = dic_dwt53_low_size(width);
        height = dic_dwt53_low_size(height);
    }

    return DIC_STATUS_OK;
}

static inline int32_t dic_dwt53_floor_div2(int32_t value) {
    return value >= 0 ? value / 2 : -(((-value) + 1) / 2);
}

static inline int32_t dic_dwt53_floor_div4(int32_t value) {
    return value >= 0 ? value / 4 : -(((-value) + 3) / 4);
}

/**
 * @brief lifting scheme implementation of the 5/3 wavelet transform for 1d
 * @details 1. split the input into even and odd samples, 2. update the odd
 * samples using the even samples, get the high coefficients, 3. update the even
 * samples using the odd samples, get the low coefficients, 4. interleave the
 * low and high coefficients back into the input array
 */
static dic_status dic_dwt53_forward_1d(int32_t* samples, int length,
                                       int32_t* buffer) {
    if (samples == NULL || buffer == NULL || length <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (length == 1) return DIC_STATUS_OK;

    int low_count = dic_dwt53_low_size(length);
    int high_count = dic_dwt53_high_size(length);
    int32_t* low = buffer;
    int32_t* high = buffer + low_count;

    for (int i = 0; i < low_count; i++) low[i] = samples[i * 2];
    for (int i = 0; i < high_count; i++) high[i] = samples[(i * 2) + 1];
    
    // gen high coefficients
    for (int i = 0; i < high_count; i++) {
        int32_t left = low[i];
        // odd should be the average of the two even samples, other wise there will be high frequency changes.
        // (low_count - 1) for mirror in the boundary case, is equivalent to symmetric extension in the wavelet transform.
        int32_t right = low[(i + 1) < low_count ? (i + 1) : (low_count - 1)];
        // update by subtracting the average of the two even samples, get the high coefficients.
        high[i] -= dic_dwt53_floor_div2(left + right);
    }

    // gen low coefficients
    for (int i = 0; i < low_count; i++) {
        if (high_count == 0) break;

        int32_t left = high[i > 0 ? (i - 1) : 0];
        int32_t right = high[i < high_count ? i : (high_count - 1)];
        low[i] += dic_dwt53_floor_div4(left + right + 2);
    }

    // interleave low and high coefficients back into the input array
    for (int i = 0; i < low_count; i++) samples[i] = low[i];
    for (int i = 0; i < high_count; i++) samples[low_count + i] = high[i];

    return DIC_STATUS_OK;
}

/**
 * @brief inverse lifting scheme implementation of the 5/3 wavelet transform for 1d
 */
static dic_status dic_dwt53_inverse_1d(int32_t* samples, int length,
                                       int32_t* buffer) {
    if (samples == NULL || buffer == NULL || length <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (length == 1) return DIC_STATUS_OK;

    int low_count = dic_dwt53_low_size(length);
    int high_count = dic_dwt53_high_size(length);
    int32_t* low = buffer;
    int32_t* high = buffer + low_count;

    // split the input into low and high coefficients by count
    for (int i = 0; i < low_count; i++) low[i] = samples[i];
    for (int i = 0; i < high_count; i++) high[i] = samples[low_count + i];

    // recover the even samples by subtracting the average of the two odd samples
    for (int i = 0; i < low_count; i++) {
        int32_t left;
        int32_t right;

        if (high_count == 0) break;

        left = high[i > 0 ? (i - 1) : 0];
        right = high[i < high_count ? i : (high_count - 1)];
        low[i] -= dic_dwt53_floor_div4(left + right + 2);
    }

    // recover the odd samples by adding the average of the two even samples
    for (int i = 0; i < high_count; i++) {
        int32_t left = low[i];
        int32_t right = low[(i + 1) < low_count ? (i + 1) : (low_count - 1)];
        high[i] += dic_dwt53_floor_div2(left + right);
    }

    // recover the index of even and odd
    for (int i = 0; i < high_count; i++) {
        samples[i * 2] = low[i];
        samples[(i * 2) + 1] = high[i];
    }
    if (low_count > high_count) samples[length - 1] = low[low_count - 1];

    return DIC_STATUS_OK;
}

static dic_status dic_dwt53_transform_rows(int32_t* plane, int stride,
                                           int width, int height, int inverse,
                                           int32_t* buffer) {

    for (int y = 0; y < height; y++) {
        int32_t* row = plane + ((size_t)y * (size_t)stride);
        dic_status status = inverse ? dic_dwt53_inverse_1d(row, width, buffer)
                                    : dic_dwt53_forward_1d(row, width, buffer);
        if (status != DIC_STATUS_OK) return status;
    }

    return DIC_STATUS_OK;
}

static dic_status dic_dwt53_transform_columns(int32_t* plane, int stride,
                                              int width, int height,
                                              int inverse, int32_t* buffer) {
    for (int x = 0; x < width; ++x) {
        dic_status status;

        for (int y = 0; y < height; y++)
            buffer[y] = plane[((size_t)y * (size_t)stride) + (size_t)x];

        status = inverse
                     ? dic_dwt53_inverse_1d(buffer, height, buffer + height)
                     : dic_dwt53_forward_1d(buffer, height, buffer + height);
        if (status != DIC_STATUS_OK) return status;

        for (int y = 0; y < height; y++)
            plane[((size_t)y * (size_t)stride) + (size_t)x] = buffer[y];
    }

    return DIC_STATUS_OK;
}

static dic_status dic_dwt53_transform_plane(int32_t* plane, int width,
                                            int height, int levels,
                                            int inverse) {
    int32_t* buffer = NULL;
    dic_status status;
    int current_width;
    int current_height;

    if (plane == NULL) return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK) return status;

    int max_dimension = width > height ? width : height;
    // pre allocate reuseable buffer
    buffer = (int32_t*)malloc((size_t)max_dimension * 2u * sizeof(int32_t));
    if (buffer == NULL) return DIC_STATUS_MEMORY_ERROR;

    if (!inverse) {
        current_width = width;
        current_height = height;
        status = DIC_STATUS_OK;
        for (int level = 1; level <= levels; ++level) {
            // stride always use the original width, for data still stored as original width
            status = dic_dwt53_transform_columns(plane, width, current_width,
                                                 current_height, 0, buffer);
            if (status != DIC_STATUS_OK) break;

            status = dic_dwt53_transform_rows(plane, width, current_width,
                                              current_height, 0, buffer);
            if (status != DIC_STATUS_OK) break;
            
            // focus on LL subband.
            current_width = dic_dwt53_low_size(current_width);
            current_height = dic_dwt53_low_size(current_height);
        }
    } else {
        int* widths = (int*)malloc((size_t)levels * sizeof(int));
        int* heights = (int*)malloc((size_t)levels * sizeof(int));
        if (widths == NULL || heights == NULL) {
            free(widths);
            free(heights);
            free(buffer);
            return DIC_STATUS_MEMORY_ERROR;
        }

        current_width = width;
        current_height = height;
        for (int level = 0; level < levels; ++level) {
            widths[level] = current_width;
            heights[level] = current_height;
            current_width = dic_dwt53_low_size(current_width);
            current_height = dic_dwt53_low_size(current_height);
        }

        status = DIC_STATUS_OK;
        for (int level = levels - 1; level >= 0; --level) {
            status = dic_dwt53_transform_rows(plane, width, widths[level],
                                              heights[level], 1, buffer);
            if (status != DIC_STATUS_OK) break;

            status = dic_dwt53_transform_columns(plane, width, widths[level],
                                                 heights[level], 1, buffer);
            if (status != DIC_STATUS_OK) break;
        }

        free(widths);
        free(heights);
    }

    free(buffer);
    return status;
}

dic_status dic_dwt53_forward_plane(int32_t* plane, int width, int height,
                                   int levels) {
    return dic_dwt53_transform_plane(plane, width, height, levels, 0);
}

dic_status dic_dwt53_inverse_plane(int32_t* plane, int width, int height,
                                   int levels) {
    return dic_dwt53_transform_plane(plane, width, height, levels, 1);
}

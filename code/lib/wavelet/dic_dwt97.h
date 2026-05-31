#pragma once

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the low-pass sample count for a JPEG 2000 dyadic split.
 */
int dic_dwt97_low_size(int length);

/**
 * Return the high-pass sample count for a JPEG 2000 dyadic split.
 */
int dic_dwt97_high_size(int length);

/**
 * Validate dimensions and decomposition level count for a 9/7 DWT.
 */
dic_status dic_dwt97_validate_levels(int width, int height, int levels);

/**
 * Apply an irreversible JPEG 2000 9/7 forward DWT to a double image plane.
 *
 * The transform is performed in-place over the top-left low-pass region at
 * each level. Samples are stored with all low-pass coefficients first, then
 * high-pass coefficients, matching the project's reversible 5/3 layout.
 */
dic_status dic_dwt97_forward_plane(
    double *plane,
    int width,
    int height,
    int levels
);

/**
 * Apply an irreversible JPEG 2000 9/7 inverse DWT to a double image plane.
 *
 * The input layout must be the in-place subband layout produced by
 * dic_dwt97_forward_plane with the same width, height, and level count.
 */
dic_status dic_dwt97_inverse_plane(
    double *plane,
    int width,
    int height,
    int levels
);

#ifdef __cplusplus
}
#endif

#pragma once

/**
 * @file j2k_ict.h
 * @brief JPEG 2000 irreversible component transform declarations.
 */

#include <stddef.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transform interleaved level-shifted RGB samples to ICT Y, Cb, Cr.
 *
 * The transform is performed in-place on triples stored as R, G, B, R, G, B.
 * `sample_count` is the number of RGB triples, not the number of doubles.
 *
 * @param samples Interleaved double samples to transform.
 * @param sample_count Number of RGB sample triples in `samples`.
 * @return DIC_STATUS_OK on success, DIC_STATUS_INVALID_ARGUMENT for NULL or
 * zero-length input.
 */
dic_status j2k_ict_forward(
    double *samples,
    size_t sample_count
);

/**
 * @brief Transform interleaved ICT Y, Cb, Cr samples back to RGB.
 *
 * The transform is performed in-place on triples stored as Y, Cb, Cr. It uses
 * the algebraic inverse of the T.800 Annex G forward ICT matrix.
 *
 * @param samples Interleaved double samples to transform.
 * @param sample_count Number of YCbCr sample triples in `samples`.
 * @return DIC_STATUS_OK on success, DIC_STATUS_INVALID_ARGUMENT for NULL or
 * zero-length input.
 */
dic_status j2k_ict_inverse(
    double *samples,
    size_t sample_count
);

#ifdef __cplusplus
}
#endif

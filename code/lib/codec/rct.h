#pragma once
/**
 * @file rct.h
 * @brief Integer-reversible RGB component transform.
 *
 * The transform follows the JPEG 2000 reversible component transform and is
 * applied to three separate signed 32-bit planes:
 *
 * @code{.unparsed}
 * forward:  R, G, B   -> Y, Cb, Cr
 * inverse:  Y, Cb, Cr -> R, G, B
 * @endcode
 *
 * No information is lost by the transform itself.
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Applies the forward RCT in place.
 *
 * On entry the planes hold R, G, and B. On return they hold Y, Cb, and Cr.
 *
 * @param r_plane R input and Y output.
 * @param g_plane G input and Cb output.
 * @param b_plane B input and Cr output.
 * @param pixel_count Number of elements in each plane.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_rct_forward(int32_t* r_plane, int32_t* g_plane,
                             int32_t* b_plane, size_t pixel_count);

/**
 * @brief Applies the inverse RCT in place.
 *
 * On entry the planes hold Y, Cb, and Cr. On return they hold R, G, and B.
 *
 * @param y_plane Y input and R output.
 * @param cb_plane Cb input and G output.
 * @param cr_plane Cr input and B output.
 * @param pixel_count Number of elements in each plane.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_rct_inverse(int32_t* y_plane, int32_t* cb_plane,
                             int32_t* cr_plane, size_t pixel_count);

#ifdef __cplusplus
}
#endif

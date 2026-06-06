#pragma once
/**
 * @file rct.h
 * @brief Reversible Component Transform (JPEG 2000 T.800 Annex G).
 *
 * Converts between RGB and YCbCr colour spaces using an integer-reversible
 * lifting transform.  Applied on three separate int32_t planes.
 *
 * Forward:  R G B  →  Y  Cb Cr
 * Inverse:  Y Cb Cr →  R  G  B
 *
 * The transform is bit-exact roundtrippable with no loss.
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Forward RCT  (RGB → YCbCr), in-place on three separate planes.
 *
 * On entry the three planes hold red, green and blue samples.
 * On exit  plane 0 = Y (luma), plane 1 = Cb (chroma-blue),
 *          plane 2 = Cr (chroma-red).
 *
 * @param r_plane       R channel in, Y out.
 * @param g_plane       G channel in, Cb out.
 * @param b_plane       B channel in, Cr out.
 * @param pixel_count   Number of pixels in each plane.
 * @return DIC_STATUS_OK on success.
 */
dic_status codec_rct_forward(int32_t* r_plane, int32_t* g_plane,
                             int32_t* b_plane, size_t pixel_count);

/**
 * @brief Inverse RCT  (YCbCr → RGB), in-place on three separate planes.
 *
 * On entry the three planes hold Y, Cb and Cr samples.
 * On exit  plane 0 = R, plane 1 = G, plane 2 = B.
 *
 * @param y_plane       Y channel in, R out.
 * @param cb_plane      Cb channel in, G out.
 * @param cr_plane      Cr channel in, B out.
 * @param pixel_count   Number of pixels in each plane.
 * @return DIC_STATUS_OK on success.
 */
dic_status codec_rct_inverse(int32_t* y_plane, int32_t* cb_plane,
                             int32_t* cr_plane, size_t pixel_count);

#ifdef __cplusplus
}
#endif

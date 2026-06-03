#pragma once
/**
 * @file subband.h
 * @brief Rectangle layout helpers for multilevel 5/3 DWT subbands.
 */

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Subband orientation in the packed wavelet coefficient plane. */
typedef enum codec_subband_orientation
{
    /** Lowest low-low approximation band. */
    DIC_SUBBAND_LL = 0,
    /** Horizontal high-pass, vertical low-pass detail band. */
    DIC_SUBBAND_HL = 1,
    /** Horizontal low-pass, vertical high-pass detail band. */
    DIC_SUBBAND_LH = 2,
    /** Horizontal high-pass, vertical high-pass detail band. */
    DIC_SUBBAND_HH = 3
} codec_subband_orientation;

/** Integer rectangle in a packed coefficient plane. */
typedef struct dic_rect_i32
{
    /** Left coordinate. */
    int x;
    /** Top coordinate. */
    int y;
    /** Rectangle width. */
    int width;
    /** Rectangle height. */
    int height;
} dic_rect_i32;

/**
 * @brief Computes the rectangle occupied by the lowest LL subband.
 * @param width Original image width.
 * @param height Original image height.
 * @param levels Number of DWT decomposition levels.
 * @param rect Output rectangle.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_subband_lowest_ll_rect(
    int width,
    int height,
    int levels,
    dic_rect_i32 *rect
);

/**
 * @brief Computes the rectangle for a requested wavelet subband.
 * @param width Original image width.
 * @param height Original image height.
 * @param levels Total number of DWT decomposition levels.
 * @param level One-based decomposition level for high bands.
 * @param orientation Requested orientation.
 * @param rect Output rectangle.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_subband_rect(
    int width,
    int height,
    int levels,
    int level,
    codec_subband_orientation orientation,
    dic_rect_i32 *rect
);

#ifdef __cplusplus
}
#endif

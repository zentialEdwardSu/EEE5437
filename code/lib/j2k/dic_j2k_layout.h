#pragma once

#include <stddef.h>

#include "codec/dic_subband.h"
#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_j2k_codeblock_grid
{
    dic_rect_i32 subband; /**< Sub-band rectangle in tile-component coefficient coordinates. */
    int codeblock_width; /**< Nominal code-block width in coefficients. */
    int codeblock_height; /**< Nominal code-block height in coefficients. */
    int blocks_x; /**< Number of code-blocks across the sub-band. */
    int blocks_y; /**< Number of code-blocks down the sub-band. */
    size_t block_count; /**< Total number of code-blocks in raster order. */
} dic_j2k_codeblock_grid;

dic_status dic_j2k_codeblock_grid_for_subband(
    int width,
    int height,
    int levels,
    int resolution,
    dic_subband_orientation orientation,
    int codeblock_width,
    int codeblock_height,
    dic_j2k_codeblock_grid *grid
);

dic_status dic_j2k_codeblock_rect(
    const dic_j2k_codeblock_grid *grid,
    int block_x,
    int block_y,
    dic_rect_i32 *rect
);

dic_status dic_j2k_resolution_subband_count(
    int levels,
    int resolution,
    int *count
);

dic_status dic_j2k_packet_count_lrcp(
    int components,
    int levels,
    int layers,
    size_t *packet_count
);

#ifdef __cplusplus
}
#endif

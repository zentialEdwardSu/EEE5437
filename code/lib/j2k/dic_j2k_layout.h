#pragma once

#include <stddef.h>
#include <stdint.h>

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

typedef struct dic_j2k_precinct_grid
{
    dic_rect_i32 subband; /**< Sub-band rectangle covered by the precinct partition. */
    int codeblock_width; /**< Code-block width used by the sub-band grid. */
    int codeblock_height; /**< Code-block height used by the sub-band grid. */
    int blocks_x; /**< Full sub-band code-block count in the horizontal direction. */
    int blocks_y; /**< Full sub-band code-block count in the vertical direction. */
    int precinct_width; /**< Precinct projection width in sub-band coefficients. */
    int precinct_height; /**< Precinct projection height in sub-band coefficients. */
    int precinct_origin_x; /**< Horizontal precinct-grid origin in tile-component coefficient coordinates. */
    int precinct_origin_y; /**< Vertical precinct-grid origin in tile-component coefficient coordinates. */
    int precincts_x; /**< Number of precincts intersecting the sub-band horizontally. */
    int precincts_y; /**< Number of precincts intersecting the sub-band vertically. */
    size_t precinct_count; /**< Total number of precincts intersecting the sub-band in raster order. */
} dic_j2k_precinct_grid;

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

/** Builds the precinct partition projected onto one sub-band code-block grid. */
dic_status dic_j2k_precinct_grid_for_subband(
    const dic_j2k_codeblock_grid *codeblock_grid,
    int resolution,
    uint8_t precinct_width_exponent,
    uint8_t precinct_height_exponent,
    dic_j2k_precinct_grid *precinct_grid
);

/** Returns the code-block window covered by one precinct in sub-band raster coordinates. */
dic_status dic_j2k_precinct_codeblock_window(
    const dic_j2k_precinct_grid *precinct_grid,
    int precinct_x,
    int precinct_y,
    int *first_block_x,
    int *first_block_y,
    int *blocks_x,
    int *blocks_y
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

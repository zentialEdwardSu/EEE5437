#pragma once

/**
 * @file j2k_layout.h
 * @brief JPEG 2000 code-block and precinct grid layout declarations.
 *
 * Implements T.800 Annex B.6-B.10 — the decomposition of tile-component
 * coefficient arrays into resolution levels, sub-bands, code-blocks, and
 * precincts. These helpers compute the geometric partitioning needed by
 * both the encoder (j2k_image.c) and decoder (j2k_decode.c) to iterate
 * sub-band code-block grids during packet construction and packet parsing.
 *
 * Key concepts:
 * - Resolution level r = 0 is the LL sub-band; r ≥ 1 contains HL, LH, HH
 *   sub-bands at decomposition level (N - r + 1) where N is the total
 *   number of DWT levels (Annex B.5, Figure B.7).
 * - A code-block grid partitions each sub-band into fixed-size code-blocks
 *   (typically 64×64 coefficients); edge code-blocks are clipped to the
 *   sub-band boundary (Annex B.7).
 * - Precincts group code-blocks into spatial regions for progression
 *   ordering; the precinct size in sub-band coefficients is
 *   2^(PPx - (r > 0 ? 1 : 0)) × 2^(PPy - (r > 0 ? 1 : 0)) (Annex B.6).
 * - LRCP packet order visits layers, then resolutions, then components,
 *   then precinct positions (Annex B.10.8, Table A.16).
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex B.6 (precincts)
 * - paper/T-REC-T.800-200208.pdf, Annex B.7 (code-blocks)
 * - paper/T-REC-T.800-200208.pdf, Annex B.10.8 (packet progression)
 */

#include <stddef.h>
#include <stdint.h>

#include "codec/subband.h"
#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Code-block grid covering one sub-band.
 *
 * Describes the rectangular tiling of a sub-band into fixed-size
 * code-blocks. Each code-block is indexed in raster order (bx, by)
 * where 0 ≤ bx < blocks_x and 0 ≤ by < blocks_y.
 */
typedef struct j2k_codeblock_grid
{
    /** Sub-band rectangle in tile-component coefficient coordinates. */
    dic_rect_i32 subband;
    /** Nominal code-block width in coefficients (typically 64). */
    int codeblock_width;
    /** Nominal code-block height in coefficients (typically 64). */
    int codeblock_height;
    /** Number of code-blocks across the sub-band (ceil(w / cbw)). */
    int blocks_x;
    /** Number of code-blocks down the sub-band (ceil(h / cbh)). */
    int blocks_y;
    /** Total number of code-blocks: blocks_x × blocks_y. */
    size_t block_count;
} j2k_codeblock_grid;

/**
 * @brief Precinct partition projected onto one sub-band.
 *
 * A precinct groups a contiguous window of code-blocks from a single
 * sub-band. The precinct origin is aligned to a global grid anchored
 * at the tile-component origin (0, 0).
 */
typedef struct j2k_precinct_grid
{
    /** Sub-band rectangle covered by the precinct partition. */
    dic_rect_i32 subband;
    /** Code-block width used by the sub-band grid. */
    int codeblock_width;
    /** Code-block height used by the sub-band grid. */
    int codeblock_height;
    /** Full sub-band code-block count in the horizontal direction. */
    int blocks_x;
    /** Full sub-band code-block count in the vertical direction. */
    int blocks_y;
    /** Precinct projection width in sub-band coefficients. */
    int precinct_width;
    /** Precinct projection height in sub-band coefficients. */
    int precinct_height;
    /** Horizontal precinct-grid origin (floor-aligned to precinct_width). */
    int precinct_origin_x;
    /** Vertical precinct-grid origin (floor-aligned to precinct_height). */
    int precinct_origin_y;
    /** Number of precincts intersecting the sub-band horizontally. */
    int precincts_x;
    /** Number of precincts intersecting the sub-band vertically. */
    int precincts_y;
    /** Total precinct count: precincts_x × precincts_y. */
    size_t precinct_count;
} j2k_precinct_grid;

/**
 * @brief Compute the code-block grid for one resolution-level sub-band.
 *
 * Combines the sub-band rectangle from codec_subband_rect() with the
 * requested code-block dimensions. For resolution 0 (LL), orientation
 * must be DIC_SUBBAND_LL. For resolutions ≥ 1, orientation must be
 * HL, LH, or HH.
 *
 * @param width Tile-component coefficient-plane width.
 * @param height Tile-component coefficient-plane height.
 * @param levels Total DWT decomposition levels.
 * @param resolution Resolution level (0 = LL, 1…levels = HL/LH/HH).
 * @param orientation Sub-band orientation.
 * @param codeblock_width Nominal code-block width (> 0).
 * @param codeblock_height Nominal code-block height (> 0).
 * @param grid [out] Receives the computed code-block grid.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT for invalid parameters.
 */
dic_status j2k_codeblock_grid_for_subband(
    int width,
    int height,
    int levels,
    int resolution,
    codec_subband_orientation orientation,
    int codeblock_width,
    int codeblock_height,
    j2k_codeblock_grid *grid
);

/**
 * @brief Compute the coordinate rectangle of one code-block.
 *
 * Edge code-blocks are clipped to the sub-band boundary.
 *
 * @param grid Code-block grid from j2k_codeblock_grid_for_subband().
 * @param block_x Horizontal code-block index (0 ≤ block_x < grid->blocks_x).
 * @param block_y Vertical code-block index (0 ≤ block_y < grid->blocks_y).
 * @param rect [out] Receives the code-block rectangle in tile-component
 *             coefficient coordinates.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if indices are out of bounds.
 */
dic_status j2k_codeblock_rect(
    const j2k_codeblock_grid *grid,
    int block_x,
    int block_y,
    dic_rect_i32 *rect
);

/**
 * @brief Build the precinct partition projected onto a code-block grid.
 *
 * Computes the precinct geometry (size, origin, counts) from the
 * code-block grid and the COD marker precinct exponents PPx/PPy.
 * The precinct size in sub-band coefficients is:
 *   2^PPx / 2^(resolution > 0 ? 1 : 0)  ×  2^PPy / 2^(resolution > 0 ? 1 : 0)
 *
 * @param codeblock_grid Sub-band code-block grid.
 * @param resolution Resolution level.
 * @param precinct_width_exponent COD PPx value.
 * @param precinct_height_exponent COD PPy value.
 * @param precinct_grid [out] Receives the computed precinct grid.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT for invalid parameters or empty
 *         precinct sizes.
 */
dic_status j2k_precinct_grid_for_subband(
    const j2k_codeblock_grid *codeblock_grid,
    int resolution,
    uint8_t precinct_width_exponent,
    uint8_t precinct_height_exponent,
    j2k_precinct_grid *precinct_grid
);

/**
 * @brief Return the code-block window covered by one precinct.
 *
 * Maps a precinct index (px, py) to the range of code-block indices
 * (first_block_x … first_block_x + blocks_x - 1). A precinct that
 * does not intersect the sub-band returns DIC_STATUS_INVALID_ARGUMENT.
 *
 * @param precinct_grid Precinct partition from j2k_precinct_grid_for_subband().
 * @param precinct_x Horizontal precinct index.
 * @param precinct_y Vertical precinct index.
 * @param first_block_x [out] First code-block column in the precinct window.
 * @param first_block_y [out] First code-block row in the precinct window.
 * @param blocks_x [out] Number of code-block columns in the window (> 0).
 * @param blocks_y [out] Number of code-block rows in the window (> 0).
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if the precinct does not intersect
 *         the sub-band or parameters are invalid.
 */
dic_status j2k_precinct_codeblock_window(
    const j2k_precinct_grid *precinct_grid,
    int precinct_x,
    int precinct_y,
    int *first_block_x,
    int *first_block_y,
    int *blocks_x,
    int *blocks_y
);

/**
 * @brief Count sub-bands at a given resolution level.
 *
 * Resolution 0 (LL) returns 1. Resolutions ≥ 1 return 3 (HL, LH, HH).
 *
 * @param levels Total DWT decomposition levels.
 * @param resolution Resolution level (0…levels).
 * @param count [out] Receives the sub-band count (1 or 3).
 * @return DIC_STATUS_OK on success.
 * @return DIC_J2K_INVALID_LEVELS if resolution > levels.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p count is NULL.
 */
dic_status j2k_resolution_subband_count(
    int levels,
    int resolution,
    int *count
);

/**
 * @brief Compute the total packet count for LRCP progression.
 *
 * For L layers, D decomposition levels, and C components, the total
 * number of packets in LRCP order (Annex B.10.8, Table A.16) is:
 *   L × (D + 1) × C
 *
 * @param components Number of image components.
 * @param levels Number of DWT decomposition levels.
 * @param layers Number of quality layers.
 * @param packet_count [out] Receives the computed packet count.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT for invalid parameters or overflow.
 */
dic_status j2k_packet_count_lrcp(
    int components,
    int levels,
    int layers,
    size_t *packet_count
);

#ifdef __cplusplus
}
#endif

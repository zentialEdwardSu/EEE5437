/**
 * @file dic_j2k_layout.c
 * @brief Implements JPEG 2000 sub-band and code-block layout helpers from T.800 Annex B.
 *
 * The helpers compute resolution-level sub-band rectangles, code-block grids, individual
 * code-block rectangles, and LRCP packet counts for the project's single-precinct layout.
 * The behavior is narrower than the standard because precinct partition exponents and tile
 * origins are fixed by the surrounding encoder.
 *
 * References: dic_subband.h for project sub-band rectangles, dic_j2k_packet.c for packet
 * ordering users, dic_j2k_image.c for encoder layout, and Annex J examples that show
 * sub-band/code-block decoding steps.
 */

#include "j2k/dic_j2k_layout.h"
#include "j2k/dic_j2k_debug.h"

#include <stdint.h>

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.2-B.6, tile-components are partitioned into resolution/sub-band regions. */
dic_status dic_j2k_resolution_subband_count(
    int levels,
    int resolution,
    int *count
)
{
    DIC_J2K_DEBUG_ENTER();
    if (count == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (levels < 0 || resolution < 0 || resolution > levels)
        return DIC_J2K_INVALID_LEVELS;

    *count = resolution == 0 ? 1 : 3;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.7, code-block dimensions partition each sub-band. */
dic_status dic_j2k_codeblock_grid_for_subband(
    int width,
    int height,
    int levels,
    int resolution,
    dic_subband_orientation orientation,
    int codeblock_width,
    int codeblock_height,
    dic_j2k_codeblock_grid *grid
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_status status;
    int level;

    if (grid == NULL || codeblock_width <= 0 || codeblock_height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (resolution < 0 || resolution > levels)
        return DIC_J2K_INVALID_LEVELS;
    if (resolution == 0 && orientation != DIC_SUBBAND_LL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (resolution > 0 && orientation == DIC_SUBBAND_LL)
        return DIC_STATUS_INVALID_ARGUMENT;

    level = resolution == 0 ? levels : (levels - resolution + 1);
    status = dic_subband_rect(width, height, levels, level, orientation, &grid->subband);
    if (status != DIC_STATUS_OK)
        return status;

    grid->codeblock_width = codeblock_width;
    grid->codeblock_height = codeblock_height;
    grid->blocks_x = (grid->subband.width + codeblock_width - 1) / codeblock_width;
    grid->blocks_y = (grid->subband.height + codeblock_height - 1) / codeblock_height;
    grid->block_count = (size_t)grid->blocks_x * (size_t)grid->blocks_y;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.7, edge code-blocks are clipped to the sub-band boundary. */
dic_status dic_j2k_codeblock_rect(
    const dic_j2k_codeblock_grid *grid,
    int block_x,
    int block_y,
    dic_rect_i32 *rect
)
{
    DIC_J2K_DEBUG_ENTER();
    int x0;
    int y0;
    int x1;
    int y1;

    if (grid == NULL || rect == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (block_x < 0 || block_y < 0 || block_x >= grid->blocks_x || block_y >= grid->blocks_y)
        return DIC_STATUS_INVALID_ARGUMENT;

    x0 = grid->subband.x + block_x * grid->codeblock_width;
    y0 = grid->subband.y + block_y * grid->codeblock_height;
    x1 = x0 + grid->codeblock_width;
    y1 = y0 + grid->codeblock_height;
    if (x1 > grid->subband.x + grid->subband.width)
        x1 = grid->subband.x + grid->subband.width;
    if (y1 > grid->subband.y + grid->subband.height)
        y1 = grid->subband.y + grid->subband.height;

    rect->x = x0;
    rect->y = y0;
    rect->width = x1 - x0;
    rect->height = y1 - y0;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Table A.16 and B.10.8, LRCP emits layer-resolution-component-position packets. */
dic_status dic_j2k_packet_count_lrcp(
    int components,
    int levels,
    int layers,
    size_t *packet_count
)
{
    DIC_J2K_DEBUG_ENTER();
    uint64_t count;

    if (packet_count == NULL || components <= 0 || layers <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (levels < 0 || levels > 32)
        return DIC_J2K_INVALID_LEVELS;

    count = (uint64_t)layers * (uint64_t)(levels + 1) * (uint64_t)components;
    if (count > (uint64_t)((size_t)-1))
        return DIC_STATUS_INVALID_ARGUMENT;

    *packet_count = (size_t)count;
    return DIC_STATUS_OK;
}

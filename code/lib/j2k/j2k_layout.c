/**
 * @file j2k_layout.c
 * @brief Implements JPEG 2000 sub-band and code-block layout helpers from T.800 Annex B.
 *
 * The helpers compute resolution-level sub-band rectangles, code-block grids, individual
 * code-block rectangles, precinct projections, and LRCP packet counts. Precinct helpers
 * expose the sub-band code-block windows that packet construction uses when a resolution is
 * split into multiple packet positions.
 *
 * References: subband.h for project sub-band rectangles, j2k_packet.c for packet
 * ordering users, j2k_image.c for encoder layout, and Annex J examples that show
 * sub-band/code-block decoding steps.
 */

#include "j2k/j2k_layout.h"
#include "j2k/j2k_debug.h"

#include <stdint.h>

static int j2k_floor_to_multiple(int value, int step)
{
    j2k_DEBUG_ENTER();
    if (value >= 0)
        return (value / step) * step;
    return -(((-value + step - 1) / step) * step);
}

static int j2k_ceil_to_multiple(int value, int step)
{
    j2k_DEBUG_ENTER();
    if (value >= 0)
        return ((value + step - 1) / step) * step;
    return -((-value / step) * step);
}

static int j2k_ceil_div_positive(int value, int divisor)
{
    j2k_DEBUG_ENTER();
    return (value + divisor - 1) / divisor;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.2-B.6, tile-components are partitioned into resolution/sub-band regions. */
dic_status j2k_resolution_subband_count(
    int levels,
    int resolution,
    int *count
)
{
    j2k_DEBUG_ENTER();
    if (count == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (levels < 0 || resolution < 0 || resolution > levels)
        return DIC_J2K_INVALID_LEVELS;

    *count = resolution == 0 ? 1 : 3;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.7, code-block dimensions partition each sub-band. */
dic_status j2k_codeblock_grid_for_subband(
    int width,
    int height,
    int levels,
    int resolution,
    codec_subband_orientation orientation,
    int codeblock_width,
    int codeblock_height,
    j2k_codeblock_grid *grid
)
{
    j2k_DEBUG_ENTER();
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
    status = codec_subband_rect(width, height, levels, level, orientation, &grid->subband);
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
dic_status j2k_codeblock_rect(
    const j2k_codeblock_grid *grid,
    int block_x,
    int block_y,
    dic_rect_i32 *rect
)
{
    j2k_DEBUG_ENTER();
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

/* Reference: paper/T-REC-T.800-200208.pdf, B.6, precincts are resolution-grid partitions projected onto sub-bands. */
dic_status j2k_precinct_grid_for_subband(
    const j2k_codeblock_grid *codeblock_grid,
    int resolution,
    uint8_t precinct_width_exponent,
    uint8_t precinct_height_exponent,
    j2k_precinct_grid *precinct_grid
)
{
    j2k_DEBUG_ENTER();
    int divisor;
    int right;
    int bottom;

    if (codeblock_grid == NULL || precinct_grid == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (resolution < 0 || precinct_width_exponent > 30u || precinct_height_exponent > 30u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (codeblock_grid->subband.width <= 0 || codeblock_grid->subband.height <= 0
        || codeblock_grid->codeblock_width <= 0 || codeblock_grid->codeblock_height <= 0
        || codeblock_grid->blocks_x <= 0 || codeblock_grid->blocks_y <= 0)
    {
        return DIC_STATUS_INVALID_ARGUMENT;
    }

    divisor = resolution == 0 ? 1 : 2;
    precinct_grid->subband = codeblock_grid->subband;
    precinct_grid->codeblock_width = codeblock_grid->codeblock_width;
    precinct_grid->codeblock_height = codeblock_grid->codeblock_height;
    precinct_grid->blocks_x = codeblock_grid->blocks_x;
    precinct_grid->blocks_y = codeblock_grid->blocks_y;
    precinct_grid->precinct_width = (int)(1u << precinct_width_exponent) / divisor;
    precinct_grid->precinct_height = (int)(1u << precinct_height_exponent) / divisor;
    if (precinct_grid->precinct_width <= 0 || precinct_grid->precinct_height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    right = codeblock_grid->subband.x + codeblock_grid->subband.width;
    bottom = codeblock_grid->subband.y + codeblock_grid->subband.height;
    precinct_grid->precinct_origin_x = j2k_floor_to_multiple(
        codeblock_grid->subband.x,
        precinct_grid->precinct_width
    );
    precinct_grid->precinct_origin_y = j2k_floor_to_multiple(
        codeblock_grid->subband.y,
        precinct_grid->precinct_height
    );
    precinct_grid->precincts_x = (
        j2k_ceil_to_multiple(right, precinct_grid->precinct_width)
        - precinct_grid->precinct_origin_x
    ) / precinct_grid->precinct_width;
    precinct_grid->precincts_y = (
        j2k_ceil_to_multiple(bottom, precinct_grid->precinct_height)
        - precinct_grid->precinct_origin_y
    ) / precinct_grid->precinct_height;
    precinct_grid->precinct_count = (size_t)precinct_grid->precincts_x * (size_t)precinct_grid->precincts_y;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.7-B.10, each packet position contains the code-blocks intersecting its precinct. */
dic_status j2k_precinct_codeblock_window(
    const j2k_precinct_grid *precinct_grid,
    int precinct_x,
    int precinct_y,
    int *first_block_x,
    int *first_block_y,
    int *blocks_x,
    int *blocks_y
)
{
    j2k_DEBUG_ENTER();
    int precinct_left;
    int precinct_top;
    int start_x;
    int start_y;
    int end_x;
    int end_y;
    int first_x;
    int first_y;
    int end_block_x;
    int end_block_y;

    if (precinct_grid == NULL || first_block_x == NULL || first_block_y == NULL
        || blocks_x == NULL || blocks_y == NULL)
    {
        return DIC_STATUS_INVALID_ARGUMENT;
    }
    if (precinct_x < 0 || precinct_y < 0
        || precinct_x >= precinct_grid->precincts_x || precinct_y >= precinct_grid->precincts_y)
    {
        return DIC_STATUS_INVALID_ARGUMENT;
    }

    precinct_left = precinct_grid->precinct_origin_x + precinct_x * precinct_grid->precinct_width;
    precinct_top = precinct_grid->precinct_origin_y + precinct_y * precinct_grid->precinct_height;
    start_x = precinct_left > precinct_grid->subband.x ? precinct_left : precinct_grid->subband.x;
    start_y = precinct_top > precinct_grid->subband.y ? precinct_top : precinct_grid->subband.y;
    end_x = precinct_left + precinct_grid->precinct_width;
    end_y = precinct_top + precinct_grid->precinct_height;
    if (end_x > precinct_grid->subband.x + precinct_grid->subband.width)
        end_x = precinct_grid->subband.x + precinct_grid->subband.width;
    if (end_y > precinct_grid->subband.y + precinct_grid->subband.height)
        end_y = precinct_grid->subband.y + precinct_grid->subband.height;
    if (start_x >= end_x || start_y >= end_y)
        return DIC_STATUS_INVALID_ARGUMENT;

    first_x = (start_x - precinct_grid->subband.x) / precinct_grid->codeblock_width;
    first_y = (start_y - precinct_grid->subband.y) / precinct_grid->codeblock_height;
    end_block_x = j2k_ceil_div_positive(
        end_x - precinct_grid->subband.x,
        precinct_grid->codeblock_width
    );
    end_block_y = j2k_ceil_div_positive(
        end_y - precinct_grid->subband.y,
        precinct_grid->codeblock_height
    );
    if (end_block_x > precinct_grid->blocks_x)
        end_block_x = precinct_grid->blocks_x;
    if (end_block_y > precinct_grid->blocks_y)
        end_block_y = precinct_grid->blocks_y;

    *first_block_x = first_x;
    *first_block_y = first_y;
    *blocks_x = end_block_x - first_x;
    *blocks_y = end_block_y - first_y;
    return *blocks_x > 0 && *blocks_y > 0 ? DIC_STATUS_OK : DIC_STATUS_INVALID_ARGUMENT;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Table A.16 and B.10.8, LRCP emits layer-resolution-component-position packets. */
dic_status j2k_packet_count_lrcp(
    int components,
    int levels,
    int layers,
    size_t *packet_count
)
{
    j2k_DEBUG_ENTER();
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

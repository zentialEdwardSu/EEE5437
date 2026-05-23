#include "j2k/dic_j2k_layout.h"
#include "test_helpers.h"

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.6-B.10, sub-band/code-block layout and LRCP packet ordering. */
int main(void)
{
    dic_j2k_codeblock_grid grid;
    dic_j2k_precinct_grid precinct_grid;
    dic_rect_i32 rect;
    int subbands;
    int first_block_x;
    int first_block_y;
    int blocks_x;
    int blocks_y;
    size_t packets;

    DIC_EXPECT(dic_j2k_resolution_subband_count(5, 0, &subbands) == DIC_STATUS_OK);
    DIC_EXPECT(subbands == 1);
    DIC_EXPECT(dic_j2k_resolution_subband_count(5, 3, &subbands) == DIC_STATUS_OK);
    DIC_EXPECT(subbands == 3);

    DIC_EXPECT(dic_j2k_codeblock_grid_for_subband(
        130,
        97,
        3,
        3,
        DIC_SUBBAND_HH,
        64,
        64,
        &grid
    ) == DIC_STATUS_OK);
    DIC_EXPECT(grid.block_count >= 1u);
    DIC_EXPECT(dic_j2k_codeblock_rect(&grid, grid.blocks_x - 1, grid.blocks_y - 1, &rect) == DIC_STATUS_OK);
    DIC_EXPECT(rect.width > 0);
    DIC_EXPECT(rect.height > 0);
    DIC_EXPECT(rect.x + rect.width <= grid.subband.x + grid.subband.width);
    DIC_EXPECT(rect.y + rect.height <= grid.subband.y + grid.subband.height);

    grid.subband.x = 0;
    grid.subband.y = 0;
    grid.subband.width = 128;
    grid.subband.height = 32;
    grid.codeblock_width = 32;
    grid.codeblock_height = 32;
    grid.blocks_x = 4;
    grid.blocks_y = 1;
    grid.block_count = 4u;
    DIC_EXPECT(dic_j2k_precinct_grid_for_subband(&grid, 1, 6u, 6u, &precinct_grid) == DIC_STATUS_OK);
    DIC_EXPECT(precinct_grid.precinct_width == 32);
    DIC_EXPECT(precinct_grid.precinct_height == 32);
    DIC_EXPECT(precinct_grid.precincts_x == 4);
    DIC_EXPECT(precinct_grid.precincts_y == 1);
    DIC_EXPECT(dic_j2k_precinct_codeblock_window(
        &precinct_grid,
        2,
        0,
        &first_block_x,
        &first_block_y,
        &blocks_x,
        &blocks_y
    ) == DIC_STATUS_OK);
    DIC_EXPECT(first_block_x == 2);
    DIC_EXPECT(first_block_y == 0);
    DIC_EXPECT(blocks_x == 1);
    DIC_EXPECT(blocks_y == 1);

    DIC_EXPECT(dic_j2k_packet_count_lrcp(3, 5, 2, &packets) == DIC_STATUS_OK);
    DIC_EXPECT(packets == 36u);

    return 0;
}

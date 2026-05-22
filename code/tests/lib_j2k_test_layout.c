#include "j2k/dic_j2k_layout.h"
#include "test_helpers.h"

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.6-B.10, sub-band/code-block layout and LRCP packet ordering. */
int main(void)
{
    dic_j2k_codeblock_grid grid;
    dic_rect_i32 rect;
    int subbands;
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

    DIC_EXPECT(dic_j2k_packet_count_lrcp(3, 5, 2, &packets) == DIC_STATUS_OK);
    DIC_EXPECT(packets == 36u);

    return 0;
}

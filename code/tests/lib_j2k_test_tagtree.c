#include "j2k/j2k_tagtree.h"
#include "test_helpers.h"

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.2, tag-tree leaf tests emit zero-runs followed by one when threshold is reached. */
int main(void)
{
    j2k_tagtree tree;
    j2k_packet_header header;

    j2k_tagtree_init(&tree);
    j2k_packet_header_init(&header);

    DIC_EXPECT(j2k_tagtree_alloc(&tree, 2, 2) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_tagtree_set(&tree, 1, 0, 2u) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_tagtree_encode_leaf(&tree, 1, 0, 1u, &header) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_packet_header_finish(&header) == DIC_STATUS_OK);
    DIC_EXPECT(header.size == 1u);
    DIC_EXPECT(header.data[0] == 0x00u);
    j2k_packet_header_free(&header);

    j2k_packet_header_init(&header);
    DIC_EXPECT(j2k_tagtree_encode_leaf(&tree, 1, 0, 2u, &header) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_packet_header_finish(&header) == DIC_STATUS_OK);
    DIC_EXPECT(header.size == 1u);
    DIC_EXPECT(header.data[0] == 0x20u);

    j2k_packet_header_free(&header);
    j2k_tagtree_free(&tree);
    return 0;
}

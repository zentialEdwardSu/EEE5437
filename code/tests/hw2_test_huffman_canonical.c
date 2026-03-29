#include "hw2/hw2_huffman.h"
#include "test_helpers.h"

static void dic_expect_code(
    const dic_hw2_huffman_tree *tree,
    size_t symbol,
    const char *bits
)
{
    size_t index = 0;
    size_t expected_length = 0;
    const dic_hw2_huffman_code *code = &tree->codes[symbol];

    while (bits[expected_length] != '\0')
        ++expected_length;

    DIC_EXPECT(code->bit_length == expected_length);
    for (index = 0; index < expected_length; ++index)
        DIC_EXPECT(code->bits[index] == (unsigned char)(bits[index] == '1'));
}

int main(void)
{
    const double probabilities[] = {0.4, 0.3, 0.2, 0.1};
    dic_hw2_huffman_tree tree;
    dic_hw2_huffman_status status;

    dic_hw2_huffman_tree_init(&tree);
    status = dic_hw2_huffman_build(probabilities, 4, &tree);

    DIC_EXPECT(status == DIC_HW2_HUFFMAN_OK);
    DIC_EXPECT(tree.active_symbols == 4);
    DIC_EXPECT(tree.max_code_bits == 3);

    /*
     * For lengths {1, 2, 3, 3}, canonical Huffman assignment must be:
     * symbol 0 -> 0
     * symbol 1 -> 10
     * symbol 2 -> 110
     * symbol 3 -> 111
     */
    dic_expect_code(&tree, 0, "0");
    dic_expect_code(&tree, 1, "10");
    dic_expect_code(&tree, 2, "110");
    dic_expect_code(&tree, 3, "111");

    dic_hw2_huffman_tree_free(&tree);
    return 0;
}

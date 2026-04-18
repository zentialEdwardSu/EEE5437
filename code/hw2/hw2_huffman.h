#pragma once

#include "errors/errors.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief One node in the temporary merge tree used to derive code lengths.
 */
typedef struct dic_hw2_huffman_node
{
    double weight;
    int parent;
    int left;
    int right;
    size_t symbol;
    unsigned char is_leaf;
} dic_hw2_huffman_node;

/**
 * @brief One symbol's canonical code word.
 *
 * bits[] stores one bit per byte for simplicity. The array length is exactly
 * bit_length bytes.
 */
typedef struct dic_hw2_huffman_code
{
    size_t bit_length;
    unsigned char *bits;
} dic_hw2_huffman_code;

/**
 * @brief One node in the decode trie built from canonical codes.
 */
typedef struct dic_hw2_huffman_decode_node
{
    int child[2];
    size_t symbol;
    unsigned char is_leaf;
} dic_hw2_huffman_decode_node;

/**
 * @brief Complete Huffman model built at runtime.
 *
 * Important arrays:
 * - symbol_to_leaf[s] gives the leaf index for symbol s in the temporary tree.
 * - nodes[] stores the temporary merge tree used to compute code lengths.
 * - codes[] stores the final canonical code for each symbol.
 * - decode_nodes[] stores a decode trie derived from the canonical codebook.
 */
typedef struct dic_hw2_huffman_tree
{
    size_t symbol_count;
    size_t active_symbols;
    size_t node_count;
    size_t node_capacity;
    size_t max_code_bits;
    size_t decode_node_count;
    size_t decode_node_capacity;
    int root_index;
    int *symbol_to_leaf;
    dic_hw2_huffman_node *nodes;
    dic_hw2_huffman_code *codes;
    dic_hw2_huffman_decode_node *decode_nodes;
} dic_hw2_huffman_tree;

typedef struct dic_hw2_huffman_bitstream
{
    unsigned char *bytes;
    size_t byte_count;
    size_t bit_count;
} dic_hw2_huffman_bitstream;

typedef unsigned int (*dic_hw2_huffman_map_symbol_fn)(const void *element);
typedef void (*dic_hw2_huffman_write_symbol_fn)(void *element, unsigned int symbol);

void dic_hw2_huffman_tree_init(dic_hw2_huffman_tree *tree);
void dic_hw2_huffman_tree_free(dic_hw2_huffman_tree *tree);
void dic_hw2_huffman_bitstream_init(dic_hw2_huffman_bitstream *bitstream);
void dic_hw2_huffman_bitstream_free(dic_hw2_huffman_bitstream *bitstream);

dic_status dic_hw2_huffman_build(
    const double *probabilities,
    size_t symbol_count,
    dic_hw2_huffman_tree *tree
);

dic_status dic_hw2_huffman_build_from_counts(
    const size_t *counts,
    size_t symbol_count,
    dic_hw2_huffman_tree *tree
);

dic_status dic_hw2_huffman_encode_mapped(
    const dic_hw2_huffman_tree *tree,
    const void *symbols,
    size_t symbol_count,
    size_t symbol_size,
    dic_hw2_huffman_map_symbol_fn map_symbol,
    dic_hw2_huffman_bitstream *bitstream
);

dic_status dic_hw2_huffman_decode_mapped(
    const dic_hw2_huffman_tree *tree,
    const dic_hw2_huffman_bitstream *bitstream,
    size_t output_symbol_count,
    void *output_symbols,
    size_t symbol_size,
    dic_hw2_huffman_write_symbol_fn write_symbol
);

/**
 * @brief Register typed wrappers around the generic mapped encode/decode API.
 */
#define DIC_REGISTER_HUFFMAN_CODEC(prefix, symbol_type)                        \
    static inline unsigned int prefix##_map_symbol(const void *element)        \
    {                                                                          \
        return (unsigned int)(*(const symbol_type *)element);                  \
    }                                                                          \
                                                                               \
    static inline void prefix##_write_symbol(void *element, unsigned int symbol) \
    {                                                                          \
        *(symbol_type *)element = (symbol_type)symbol;                         \
    }                                                                          \
                                                                               \
    static inline dic_status prefix##_build(                                   \
        const double *probabilities,                                           \
        size_t symbol_count,                                                   \
        dic_hw2_huffman_tree *tree)                                            \
    {                                                                          \
        return dic_hw2_huffman_build(probabilities, symbol_count, tree);       \
    }                                                                          \
                                                                               \
    static inline dic_status prefix##_build_from_counts(                       \
        const size_t *counts,                                                  \
        size_t symbol_count,                                                   \
        dic_hw2_huffman_tree *tree)                                            \
    {                                                                          \
        return dic_hw2_huffman_build_from_counts(counts, symbol_count, tree);  \
    }                                                                          \
                                                                               \
    static inline dic_status prefix##_encode(                                  \
        const dic_hw2_huffman_tree *tree,                                      \
        const symbol_type *symbols,                                            \
        size_t symbol_count,                                                   \
        dic_hw2_huffman_bitstream *bitstream)                                  \
    {                                                                          \
        return dic_hw2_huffman_encode_mapped(                                  \
            tree,                                                              \
            symbols,                                                           \
            symbol_count,                                                      \
            sizeof(symbol_type),                                               \
            prefix##_map_symbol,                                               \
            bitstream);                                                        \
    }                                                                          \
                                                                               \
    static inline dic_status prefix##_decode(                                  \
        const dic_hw2_huffman_tree *tree,                                      \
        const dic_hw2_huffman_bitstream *bitstream,                            \
        size_t output_symbol_count,                                            \
        symbol_type *output_symbols)                                           \
    {                                                                          \
        return dic_hw2_huffman_decode_mapped(                                  \
            tree,                                                              \
            bitstream,                                                         \
            output_symbol_count,                                               \
            output_symbols,                                                    \
            sizeof(symbol_type),                                               \
            prefix##_write_symbol);                                            \
    }

#ifdef __cplusplus
}
#endif

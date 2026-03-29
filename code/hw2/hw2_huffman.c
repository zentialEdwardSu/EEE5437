#include "hw2/hw2_huffman.h"
#include "lib/pqueue/pqueue.h"

#include <stdlib.h>
#include <string.h>

typedef struct dic_hw2_huffman_heap_item
{
    double priority;
    int node_index;
    pq_node_t queue_link;
} dic_hw2_huffman_heap_item;

/**
 * @brief Sort key used while assigning canonical codes.
 *
 * Canonical Huffman codes are assigned by ordering symbols first by code length
 * and then by symbol value.
 */
typedef struct dic_hw2_huffman_length_entry
{
    size_t symbol;
    size_t bit_length;
} dic_hw2_huffman_length_entry;

static int dic_hw2_huffman_heap_compare(const pq_node_t *a, const pq_node_t *b)
{
    const dic_hw2_huffman_heap_item *left = container_of(a, dic_hw2_huffman_heap_item, queue_link);
    const dic_hw2_huffman_heap_item *right = container_of(b, dic_hw2_huffman_heap_item, queue_link);

    if (left->priority < right->priority)
        return 1;
    if (left->priority > right->priority)
        return -1;
    return 0;
}

/**
 * @brief Compare canonical sort entries by (bit_length, symbol).
 */
static int dic_hw2_huffman_compare_length_entry(const void *left_ptr, const void *right_ptr)
{
    const dic_hw2_huffman_length_entry *left = (const dic_hw2_huffman_length_entry *)left_ptr;
    const dic_hw2_huffman_length_entry *right = (const dic_hw2_huffman_length_entry *)right_ptr;

    if (left->bit_length < right->bit_length)
        return -1;
    if (left->bit_length > right->bit_length)
        return 1;
    if (left->symbol < right->symbol)
        return -1;
    if (left->symbol > right->symbol)
        return 1;
    return 0;
}

/**
 * @brief Fill a bit buffer prefix with zeros.
 *
 * Canonical code generation uses one byte per logical bit. 
 */
static void dic_hw2_huffman_zero_bits(unsigned char *bits, size_t bit_length)
{
    if (bits != NULL && bit_length > 0)
        memset(bits, 0, bit_length);
}

/**
 * @brief Increment a binary code represented as one bit per byte.
 *
 * The least significant bit is at the end of the active prefix. This helper is
 * used when moving from one canonical code to the next.
 */
static void dic_hw2_huffman_increment_bits(unsigned char *bits, size_t bit_length)
{
    size_t index = bit_length;

    while (index > 0)
    {
        --index;
        if (bits[index] == 0)
        {
            bits[index] = 1;
            return;
        }
        bits[index] = 0;
    }
}

/**
 * @brief Append one empty node to the decode trie.
 *
 * decode_nodes[] is a dense array, and child[0]/child[1] are integer indices
 * into that same array.
 */
static dic_hw2_huffman_status dic_hw2_huffman_append_decode_node(
    dic_hw2_huffman_tree *tree,
    int *out_index
)
{
    dic_hw2_huffman_decode_node *node = NULL;

    if (tree->decode_node_count >= tree->decode_node_capacity)
        return DIC_HW2_HUFFMAN_MEMORY_ERROR;

    node = &tree->decode_nodes[tree->decode_node_count];
    node->child[0] = -1;
    node->child[1] = -1;
    node->symbol = 0;
    node->is_leaf = 0;
    *out_index = (int)tree->decode_node_count;
    ++tree->decode_node_count;
    return DIC_HW2_HUFFMAN_OK;
}

/**
 * @brief Allocate top-level arrays owned by a Huffman tree.
 *
 * node_capacity depends on the number of active symbols, not the raw symbol
 * count. A full binary tree with L leaves contains exactly 2 * L - 1 nodes.
 */
static dic_hw2_huffman_status dic_hw2_huffman_allocate_tree(
    dic_hw2_huffman_tree *tree,
    size_t symbol_count,
    size_t active_symbols
)
{
    size_t symbol = 0;

    tree->symbol_count = symbol_count;
    tree->active_symbols = active_symbols;
    tree->node_capacity = active_symbols == 0 ? 0 : (active_symbols * 2) - 1;
    tree->decode_node_capacity = 1;

    tree->symbol_to_leaf = (int *)malloc(symbol_count * sizeof(int));
    tree->codes = (dic_hw2_huffman_code *)calloc(symbol_count, sizeof(dic_hw2_huffman_code));
    if (tree->symbol_to_leaf == NULL || tree->codes == NULL)
        return DIC_HW2_HUFFMAN_MEMORY_ERROR;

    for (symbol = 0; symbol < symbol_count; ++symbol)
        tree->symbol_to_leaf[symbol] = -1;

    if (tree->node_capacity > 0)
    {
        tree->nodes = (dic_hw2_huffman_node *)calloc(tree->node_capacity, sizeof(dic_hw2_huffman_node));
        if (tree->nodes == NULL)
            return DIC_HW2_HUFFMAN_MEMORY_ERROR;
    }

    return DIC_HW2_HUFFMAN_OK;
}

/**
 * @brief Derive one code length per active symbol from parent links.
 *
 * symbol_to_leaf[s] gives O(1) access to the leaf index for symbol s. Walking
 * parent links from that leaf up to the root yields the Huffman code length.
 */
static dic_hw2_huffman_status dic_hw2_huffman_collect_code_lengths(dic_hw2_huffman_tree *tree)
{
    size_t symbol = 0;

    tree->max_code_bits = 0;

    for (symbol = 0; symbol < tree->symbol_count; ++symbol)
    {
        int node_index = tree->symbol_to_leaf[symbol];
        size_t bit_length = 0;

        if (node_index < 0)
            continue;

        if (tree->active_symbols == 1)
            bit_length = 1;
        else
        {
            while (tree->nodes[node_index].parent != -1)
            {
                ++bit_length;
                node_index = tree->nodes[node_index].parent;
            }
        }

        tree->codes[symbol].bit_length = bit_length;
        if (bit_length > tree->max_code_bits)
            tree->max_code_bits = bit_length;
    }

    return DIC_HW2_HUFFMAN_OK;
}

/**
 * @brief Convert code lengths into canonical Huffman codes.
 *
 * workflow:
 * - entries[] gathers all active symbols and their lengths
 * - qsort orders entries[] by (bit_length, symbol)
 * - current_bits[] acts as a binary counter
 * - tree->codes[symbol].bits receives a dedicated copy of the canonical bits
 */
static dic_hw2_huffman_status dic_hw2_huffman_build_canonical_codes(dic_hw2_huffman_tree *tree)
{
    dic_hw2_huffman_length_entry *entries = NULL;
    unsigned char *current_bits = NULL;
    size_t entry_count = 0;
    size_t symbol = 0;
    size_t previous_length = 0;
    size_t index = 0;

    if (tree->active_symbols == 0)
        return DIC_HW2_HUFFMAN_OK;

    entries = (dic_hw2_huffman_length_entry *)malloc(tree->active_symbols * sizeof(dic_hw2_huffman_length_entry));
    current_bits = (unsigned char *)calloc(tree->max_code_bits, sizeof(unsigned char));
    if (entries == NULL || current_bits == NULL)
    {
        free(entries);
        free(current_bits);
        return DIC_HW2_HUFFMAN_MEMORY_ERROR;
    }

    for (symbol = 0; symbol < tree->symbol_count; ++symbol)
    {
        if (tree->codes[symbol].bit_length == 0)
            continue;

        entries[entry_count].symbol = symbol;
        entries[entry_count].bit_length = tree->codes[symbol].bit_length;
        ++entry_count;
    }

    qsort(entries, entry_count, sizeof(entries[0]), dic_hw2_huffman_compare_length_entry);

    for (index = 0; index < entry_count; ++index)
    {
        dic_hw2_huffman_code *code = &tree->codes[entries[index].symbol];

        if (index == 0)
        {
            dic_hw2_huffman_zero_bits(current_bits, tree->max_code_bits);
            previous_length = entries[index].bit_length;
        }
        else
        {
            /* Advance to the next canonical code of the previous length. */
            dic_hw2_huffman_increment_bits(current_bits, previous_length);

            /*
             * If the new code length is longer, canonical Huffman extends the
             * prefix by appending zeros in the newly exposed suffix.
             */
            if (entries[index].bit_length > previous_length)
                dic_hw2_huffman_zero_bits(current_bits + previous_length, entries[index].bit_length - previous_length);
            previous_length = entries[index].bit_length;
        }

        code->bits = (unsigned char *)malloc(code->bit_length * sizeof(unsigned char));
        if (code->bits == NULL)
        {
            free(entries);
            free(current_bits);
            return DIC_HW2_HUFFMAN_MEMORY_ERROR;
        }

        memcpy(code->bits, current_bits, code->bit_length);
    }

    free(entries);
    free(current_bits);
    return DIC_HW2_HUFFMAN_OK;
}

/**
 * @brief Build a decode trie from canonical codes.
 */
static dic_hw2_huffman_status dic_hw2_huffman_build_decode_trie(dic_hw2_huffman_tree *tree)
{
    size_t symbol = 0;
    size_t total_code_bits = 0;
    int root_index = -1;
    dic_hw2_huffman_status status;

    if (tree->active_symbols == 0)
        return DIC_HW2_HUFFMAN_OK;

    for (symbol = 0; symbol < tree->symbol_count; ++symbol)
        total_code_bits += tree->codes[symbol].bit_length;

    tree->decode_node_capacity = total_code_bits + 1;
    tree->decode_nodes = (dic_hw2_huffman_decode_node *)calloc(
        tree->decode_node_capacity,
        sizeof(dic_hw2_huffman_decode_node)
    );
    if (tree->decode_nodes == NULL)
        return DIC_HW2_HUFFMAN_MEMORY_ERROR;

    tree->decode_node_count = 0;
    status = dic_hw2_huffman_append_decode_node(tree, &root_index);
    if (status != DIC_HW2_HUFFMAN_OK)
        return status;
    tree->root_index = root_index;

    for (symbol = 0; symbol < tree->symbol_count; ++symbol)
    {
        const dic_hw2_huffman_code *code = &tree->codes[symbol];
        int node_index = tree->root_index;
        size_t bit_index = 0;

        if (code->bit_length == 0)
            continue;

        for (bit_index = 0; bit_index < code->bit_length; ++bit_index)
        {
            const unsigned char bit = code->bits[bit_index];
            int next_index = tree->decode_nodes[node_index].child[bit];

            if (next_index < 0)
            {
                /* Materialize this prefix only when it is first needed. */
                status = dic_hw2_huffman_append_decode_node(tree, &next_index);
                if (status != DIC_HW2_HUFFMAN_OK)
                    return status;
                tree->decode_nodes[node_index].child[bit] = next_index;
            }

            node_index = next_index;
        }

        tree->decode_nodes[node_index].is_leaf = 1;
        tree->decode_nodes[node_index].symbol = symbol;
    }

    return DIC_HW2_HUFFMAN_OK;
}

/**
 * @brief Shared tree builder for probability-based and count-based inputs.
 *
 * Huffman construction only depends on the relative ordering of weights, so
 * raw occurrence counts and normalized probabilities produce the same tree
 * shape. This helper accepts already prepared positive weights and reuses the
 * same canonical-code pipeline for both public build entry points.
 */
static dic_hw2_huffman_status dic_hw2_huffman_build_from_weights(
    const double *weights,
    size_t symbol_count,
    dic_hw2_huffman_tree *tree
)
{
    size_t active_symbols = 0;
    size_t symbol = 0;
    pqueue_t min_heap;
    dic_hw2_huffman_heap_item *heap_items = NULL;
    dic_hw2_huffman_status status;

    if (weights == NULL || tree == NULL)
        return DIC_HW2_HUFFMAN_INVALID_ARGUMENT;

    dic_hw2_huffman_tree_free(tree);

    for (symbol = 0; symbol < symbol_count; ++symbol)
    {
        if (weights[symbol] > 0.0)
            ++active_symbols;
    }

    status = dic_hw2_huffman_allocate_tree(tree, symbol_count, active_symbols);
    if (status != DIC_HW2_HUFFMAN_OK)
    {
        dic_hw2_huffman_tree_free(tree);
        return status;
    }

    if (active_symbols == 0)
        return DIC_HW2_HUFFMAN_OK;

    heap_items = (dic_hw2_huffman_heap_item *)calloc(tree->node_capacity, sizeof(dic_hw2_huffman_heap_item));
    if (heap_items == NULL)
    {
        dic_hw2_huffman_tree_free(tree);
        return DIC_HW2_HUFFMAN_MEMORY_ERROR;
    }

    pq_init(&min_heap, dic_hw2_huffman_heap_compare);

    for (symbol = 0; symbol < symbol_count; ++symbol)
    {
        if (weights[symbol] <= 0.0)
            continue;

        /*
         * Leaves are packed densely at the front of nodes[]. The current node_count
         * therefore serves both as the next free slot and as the leaf's permanent
         * index in the merge tree.
         */
        tree->symbol_to_leaf[symbol] = (int)tree->node_count;
        tree->nodes[tree->node_count].weight = weights[symbol];
        tree->nodes[tree->node_count].parent = -1;
        tree->nodes[tree->node_count].left = -1;
        tree->nodes[tree->node_count].right = -1;
        tree->nodes[tree->node_count].symbol = symbol;
        tree->nodes[tree->node_count].is_leaf = 1;

        /* heap_items[i] mirrors nodes[i], so queue pops can recover node indices. */
        heap_items[tree->node_count].priority = weights[symbol];
        heap_items[tree->node_count].node_index = (int)tree->node_count;
        list_head_init(&heap_items[tree->node_count].queue_link.list);
        pq_push(&min_heap, &heap_items[tree->node_count].queue_link);

        ++tree->node_count;
    }

    if (tree->active_symbols == 1)
    {
        tree->root_index = 0;
    }
    else
    {
        /*
         * Internal nodes are appended after the leaves. Each iteration removes
         * the two lightest available nodes, merges them into a new parent, and
         * pushes that parent back into the min-priority queue.
         */
        while (tree->node_count < tree->node_capacity)
        {
            pq_node_t *left_link = pq_pop(&min_heap);
            pq_node_t *right_link = pq_pop(&min_heap);
            dic_hw2_huffman_heap_item *left_item =
                left_link != NULL ? container_of(left_link, dic_hw2_huffman_heap_item, queue_link) : NULL;
            dic_hw2_huffman_heap_item *right_item =
                right_link != NULL ? container_of(right_link, dic_hw2_huffman_heap_item, queue_link) : NULL;
            const int left_index = left_item != NULL ? left_item->node_index : -1;
            const int right_index = right_item != NULL ? right_item->node_index : -1;
            dic_hw2_huffman_node *parent = NULL;

            if (left_index < 0 || right_index < 0 || left_index == right_index)
            {
                free(heap_items);
                dic_hw2_huffman_tree_free(tree);
                return DIC_HW2_HUFFMAN_INVALID_ARGUMENT;
            }

            tree->nodes[left_index].parent = (int)tree->node_count;
            tree->nodes[right_index].parent = (int)tree->node_count;

            parent = &tree->nodes[tree->node_count];
            parent->weight = tree->nodes[left_index].weight + tree->nodes[right_index].weight;
            parent->parent = -1;
            parent->left = left_index;
            parent->right = right_index;
            parent->symbol = 0;
            parent->is_leaf = 0;

            heap_items[tree->node_count].priority = parent->weight;
            heap_items[tree->node_count].node_index = (int)tree->node_count;
            list_head_init(&heap_items[tree->node_count].queue_link.list);
            pq_push(&min_heap, &heap_items[tree->node_count].queue_link);

            ++tree->node_count;
        }

        tree->root_index = (int)(tree->node_count - 1);
    }

    free(heap_items);

    status = dic_hw2_huffman_collect_code_lengths(tree);
    if (status != DIC_HW2_HUFFMAN_OK)
    {
        dic_hw2_huffman_tree_free(tree);
        return status;
    }

    status = dic_hw2_huffman_build_canonical_codes(tree);
    if (status != DIC_HW2_HUFFMAN_OK)
    {
        dic_hw2_huffman_tree_free(tree);
        return status;
    }

    status = dic_hw2_huffman_build_decode_trie(tree);
    if (status != DIC_HW2_HUFFMAN_OK)
    {
        dic_hw2_huffman_tree_free(tree);
        return status;
    }

    return DIC_HW2_HUFFMAN_OK;
}

void dic_hw2_huffman_tree_init(dic_hw2_huffman_tree *tree)
{
    if (tree == NULL)
        return;

    tree->symbol_count = 0;
    tree->active_symbols = 0;
    tree->node_count = 0;
    tree->node_capacity = 0;
    tree->max_code_bits = 0;
    tree->decode_node_count = 0;
    tree->decode_node_capacity = 0;
    tree->root_index = -1;
    tree->symbol_to_leaf = NULL;
    tree->nodes = NULL;
    tree->codes = NULL;
    tree->decode_nodes = NULL;
}

/**
 * @brief Release all dynamic storage owned by a Huffman tree.
 *
 * Each canonical code owns its own bits[] buffer, so those must be freed before
 * the top-level arrays are released.
 */
void dic_hw2_huffman_tree_free(dic_hw2_huffman_tree *tree)
{
    size_t symbol = 0;

    if (tree == NULL)
        return;

    if (tree->codes != NULL)
    {
        for (symbol = 0; symbol < tree->symbol_count; ++symbol)
            free(tree->codes[symbol].bits);
    }

    free(tree->symbol_to_leaf);
    free(tree->nodes);
    free(tree->codes);
    free(tree->decode_nodes);
    dic_hw2_huffman_tree_init(tree);
}

void dic_hw2_huffman_bitstream_init(dic_hw2_huffman_bitstream *bitstream)
{
    if (bitstream == NULL)
        return;

    bitstream->bytes = NULL;
    bitstream->byte_count = 0;
    bitstream->bit_count = 0;
}

void dic_hw2_huffman_bitstream_free(dic_hw2_huffman_bitstream *bitstream)
{
    if (bitstream == NULL)
        return;

    free(bitstream->bytes);
    dic_hw2_huffman_bitstream_init(bitstream);
}

dic_hw2_huffman_status dic_hw2_huffman_build(
    const double *probabilities,
    size_t symbol_count,
    dic_hw2_huffman_tree *tree
)
{
    return dic_hw2_huffman_build_from_weights(probabilities, symbol_count, tree);
}

dic_hw2_huffman_status dic_hw2_huffman_build_from_counts(
    const size_t *counts,
    size_t symbol_count,
    dic_hw2_huffman_tree *tree
)
{
    double *weights = NULL;
    size_t symbol = 0;
    dic_hw2_huffman_status status;

    if (counts == NULL || tree == NULL)
        return DIC_HW2_HUFFMAN_INVALID_ARGUMENT;

    weights = (double *)calloc(symbol_count, sizeof(double));
    if (weights == NULL)
        return DIC_HW2_HUFFMAN_MEMORY_ERROR;

    /*
     * Huffman coding only depends on relative weights, so the raw frequency
     * table from HW1 can be used directly without a normalization pass.
     */
    for (symbol = 0; symbol < symbol_count; ++symbol)
        weights[symbol] = (double)counts[symbol];

    status = dic_hw2_huffman_build_from_weights(weights, symbol_count, tree);
    free(weights);
    return status;
}

dic_hw2_huffman_status dic_hw2_huffman_encode_mapped(
    const dic_hw2_huffman_tree *tree,
    const void *symbols,
    size_t symbol_count,
    size_t symbol_size,
    dic_hw2_huffman_map_symbol_fn map_symbol,
    dic_hw2_huffman_bitstream *bitstream
)
{
    size_t index = 0;
    size_t bit_offset = 0;
    const unsigned char *cursor = (const unsigned char *)symbols;

    if (tree == NULL || map_symbol == NULL || bitstream == NULL)
        return DIC_HW2_HUFFMAN_INVALID_ARGUMENT;

    if (symbol_count > 0 && (symbols == NULL || symbol_size == 0))
        return DIC_HW2_HUFFMAN_INVALID_ARGUMENT;

    dic_hw2_huffman_bitstream_free(bitstream);

    for (index = 0; index < symbol_count; ++index)
    {
        const unsigned int symbol = map_symbol(cursor + (index * symbol_size));

        if ((size_t)symbol >= tree->symbol_count)
            return DIC_HW2_HUFFMAN_INVALID_SYMBOL;
        if (tree->codes[symbol].bit_length == 0)
            return DIC_HW2_HUFFMAN_INVALID_SYMBOL;

        bitstream->bit_count += tree->codes[symbol].bit_length;
    }

    bitstream->byte_count = (bitstream->bit_count + 7) / 8;
    if (bitstream->byte_count == 0)
        return DIC_HW2_HUFFMAN_OK;

    bitstream->bytes = (unsigned char *)calloc(bitstream->byte_count, sizeof(unsigned char));
    if (bitstream->bytes == NULL)
    {
        dic_hw2_huffman_bitstream_init(bitstream);
        return DIC_HW2_HUFFMAN_MEMORY_ERROR;
    }

    for (index = 0; index < symbol_count; ++index)
    {
        const unsigned int symbol = map_symbol(cursor + (index * symbol_size));
        const dic_hw2_huffman_code *code = &tree->codes[symbol];
        size_t bit_index = 0;

        for (bit_index = 0; bit_index < code->bit_length; ++bit_index, ++bit_offset)
        {
            /*
             * bit_offset / 8 selects the destination byte.
             * bit_offset % 8 selects the bit position within that byte.
             * Bits are packed most-significant-bit first.
             */
            if (code->bits[bit_index] != 0)
            {
                bitstream->bytes[bit_offset / 8] |=
                    (unsigned char)(1u << (7u - (unsigned int)(bit_offset % 8)));
            }
        }
    }

    return DIC_HW2_HUFFMAN_OK;
}

dic_hw2_huffman_status dic_hw2_huffman_decode_mapped(
    const dic_hw2_huffman_tree *tree,
    const dic_hw2_huffman_bitstream *bitstream,
    size_t output_symbol_count,
    void *output_symbols,
    size_t symbol_size,
    dic_hw2_huffman_write_symbol_fn write_symbol
)
{
    size_t produced = 0;
    size_t bit_offset = 0;
    unsigned char *cursor = (unsigned char *)output_symbols;
    int node_index = 0;

    if (tree == NULL || bitstream == NULL || write_symbol == NULL)
        return DIC_HW2_HUFFMAN_INVALID_ARGUMENT;

    if (output_symbol_count > 0 && (output_symbols == NULL || symbol_size == 0))
        return DIC_HW2_HUFFMAN_INVALID_ARGUMENT;

    if (output_symbol_count == 0)
        return DIC_HW2_HUFFMAN_OK;

    if (tree->active_symbols == 0 || tree->decode_nodes == NULL || tree->root_index < 0)
        return DIC_HW2_HUFFMAN_MALFORMED_BITSTREAM;

    if (tree->active_symbols == 1)
    {
        size_t symbol = 0;
        for (symbol = 0; symbol < tree->symbol_count; ++symbol)
        {
            if (tree->codes[symbol].bit_length != 0)
            {
                for (produced = 0; produced < output_symbol_count; ++produced)
                    write_symbol(cursor + (produced * symbol_size), (unsigned int)symbol);
                return DIC_HW2_HUFFMAN_OK;
            }
        }
        return DIC_HW2_HUFFMAN_MALFORMED_BITSTREAM;
    }

    node_index = tree->root_index;
    while (bit_offset < bitstream->bit_count && produced < output_symbol_count)
    {
        const unsigned char bit =
            (unsigned char)((bitstream->bytes[bit_offset / 8] >> (7u - (unsigned int)(bit_offset % 8))) & 1u);

        /* Follow the canonical decode trie using the next packed input bit. */
        node_index = tree->decode_nodes[node_index].child[bit];
        ++bit_offset;

        if (node_index < 0)
            return DIC_HW2_HUFFMAN_MALFORMED_BITSTREAM;

        if (tree->decode_nodes[node_index].is_leaf)
        {
            write_symbol(cursor + (produced * symbol_size), (unsigned int)tree->decode_nodes[node_index].symbol);
            ++produced;
            node_index = tree->root_index;
        }
    }

    if (produced != output_symbol_count || node_index != tree->root_index)
        return DIC_HW2_HUFFMAN_MALFORMED_BITSTREAM;

    return DIC_HW2_HUFFMAN_OK;
}

const char *dic_hw2_huffman_status_message(dic_hw2_huffman_status status)
{
    switch (status)
    {
    case DIC_HW2_HUFFMAN_OK:
        return "ok";
    case DIC_HW2_HUFFMAN_INVALID_ARGUMENT:
        return "invalid argument";
    case DIC_HW2_HUFFMAN_MEMORY_ERROR:
        return "memory allocation failed";
    case DIC_HW2_HUFFMAN_INVALID_SYMBOL:
        return "input contains a symbol without a Huffman code";
    case DIC_HW2_HUFFMAN_MALFORMED_BITSTREAM:
        return "malformed Huffman bitstream";
    default:
        return "unknown error";
    }
}

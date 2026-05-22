/**
 * @file dic_j2k_tagtree.c
 * @brief Implements JPEG 2000 tag-tree encoding support from T.800 Annex B.
 *
 * This file stores leaf values and emits threshold bits for packet-header inclusion and
 * zero-bitplane signalling. The public helper is a lightweight encoder-side utility; the
 * packet module contains an internal decoder-style tag-tree for payload reconstruction tests.
 *
 * References: dic_j2k_packet.h for packet-header bit output, dic_j2k_packet.c for packet
 * inclusion use, and Annex J.11 for examples of packet header decoding.
 */

#include "j2k/dic_j2k_tagtree.h"
#include "j2k/dic_j2k_debug.h"

#include <stdlib.h>

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.2, tag trees represent two-dimensional non-negative integer arrays. */
void dic_j2k_tagtree_init(dic_j2k_tagtree *tree)
{
    DIC_J2K_DEBUG_ENTER();
    if (tree == NULL)
        return;
    tree->width = 0;
    tree->height = 0;
    tree->values = NULL;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.2, tag-tree state belongs to packet-header coding. */
void dic_j2k_tagtree_free(dic_j2k_tagtree *tree)
{
    DIC_J2K_DEBUG_ENTER();
    if (tree == NULL)
        return;
    free(tree->values);
    dic_j2k_tagtree_init(tree);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Figure B.12, the lowest tag-tree level has one value per code-block. */
dic_status dic_j2k_tagtree_alloc(
    dic_j2k_tagtree *tree,
    int width,
    int height
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t count;

    if (tree == NULL || width <= 0 || height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_j2k_tagtree_free(tree);
    count = (size_t)width * (size_t)height;
    tree->values = (uint32_t *)calloc(count, sizeof(tree->values[0]));
    if (tree->values == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    tree->width = width;
    tree->height = height;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.2 and B.10.4, tag-tree leaf values hold layer/inclusion metadata. */
dic_status dic_j2k_tagtree_set(
    dic_j2k_tagtree *tree,
    int x,
    int y,
    uint32_t value
)
{
    DIC_J2K_DEBUG_ENTER();
    if (tree == NULL || tree->values == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (x < 0 || y < 0 || x >= tree->width || y >= tree->height)
        return DIC_STATUS_INVALID_ARGUMENT;

    tree->values[(size_t)y * (size_t)tree->width + (size_t)x] = value;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.2, a run of zero bits increments the tested value and a one bit confirms equality. */
static dic_status dic_j2k_tagtree_encode_value(
    uint32_t value,
    uint32_t threshold,
    dic_j2k_packet_header *header
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t cursor;
    dic_status status;

    for (cursor = 0u; cursor <= threshold && cursor < value; ++cursor)
    {
        status = dic_j2k_packet_header_append_bit(header, 0u);
        if (status != DIC_STATUS_OK)
            return status;
    }

    if (value <= threshold)
        return dic_j2k_packet_header_append_bit(header, 1u);
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.2-B.10.5, tag-tree coding is used for inclusion and zero bit-plane metadata. */
dic_status dic_j2k_tagtree_encode_leaf(
    const dic_j2k_tagtree *tree,
    int x,
    int y,
    uint32_t threshold,
    dic_j2k_packet_header *header
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t value;

    if (tree == NULL || tree->values == NULL || header == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (x < 0 || y < 0 || x >= tree->width || y >= tree->height)
        return DIC_STATUS_INVALID_ARGUMENT;

    value = tree->values[(size_t)y * (size_t)tree->width + (size_t)x];
    return dic_j2k_tagtree_encode_value(value, threshold, header);
}

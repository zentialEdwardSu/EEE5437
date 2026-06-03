#pragma once

/**
 * @file j2k_tagtree.h
 * @brief JPEG 2000 tag-tree encoding declarations.
 *
 * Tag trees (T.800 Annex B.10.2) are two-dimensional hierarchical
 * non-negative integer arrays used in packet headers to efficiently
 * encode inclusion information and zero bit-plane counts for every
 * code-block. This module provides a lightweight encoder-side helper
 * that stores leaf values and emits the threshold-comparison bitstream
 * (Annex B.10.2-B.10.5, Figure B.12).
 *
 * A tag tree of width W and height H has ceil(log2(max(W,H))) levels.
 * Each leaf stores a non-negative integer value. To encode whether a
 * value v exceeds a threshold t, the encoder emits a run of zero bits
 * equal to min(v, t) followed by a one bit if v ≤ t (confirming
 * equality), or omits the one if v > t (the decoder infers the threshold
 * from the zero-bit run length).
 *
 * This module is encoder-only; the decoder-side tag-tree state machine
 * is internal to j2k_decode.c and j2k_packet.c.
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex B.10.2-B.10.5 (tag trees)
 * - paper/T-REC-T.800-200208.pdf, Figure B.12 (tag tree example)
 * - j2k_packet.h for packet-header bit output
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"
#include "j2k/j2k_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encoder-side tag-tree state.
 *
 * Stores the leaf values of a two-dimensional tag tree.
 * Internal nodes are computed implicitly by the encoding logic.
 */
typedef struct j2k_tagtree
{
    /** Number of leaf values in the horizontal direction. */
    int width;
    /** Number of leaf values in the vertical direction. */
    int height;
    /** Leaf values stored in row-major order; size is width × height. */
    uint32_t *values;
} j2k_tagtree;

/**
 * @brief Initialise a tag tree to an empty state.
 *
 * @param tree Tag tree to initialise; NULL is silently ignored.
 */
void j2k_tagtree_init(j2k_tagtree *tree);

/**
 * @brief Release all memory owned by a tag tree.
 *
 * @param tree Tag tree to free; NULL is silently ignored.
 */
void j2k_tagtree_free(j2k_tagtree *tree);

/**
 * @brief Allocate a tag tree with the specified leaf dimensions.
 *
 * Allocates width × height leaf entries initialised to 0.
 * Any previously held storage is freed first.
 *
 * @param tree Tag tree to allocate into.
 * @param width Number of leaf values horizontally (> 0).
 * @param height Number of leaf values vertically (> 0).
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p tree is NULL or dimensions ≤ 0.
 * @return DIC_STATUS_MEMORY_ERROR if allocation fails.
 */
dic_status j2k_tagtree_alloc(
    j2k_tagtree *tree,
    int width,
    int height
);

/**
 * @brief Set the leaf value at position (x, y).
 *
 * @param tree Tag tree with an allocated leaf array.
 * @param x Horizontal leaf index (0 ≤ x < tree->width).
 * @param y Vertical leaf index (0 ≤ y < tree->height).
 * @param value Non-negative leaf value to store.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p tree is not allocated
 *         or (x, y) is out of bounds.
 */
dic_status j2k_tagtree_set(
    j2k_tagtree *tree,
    int x,
    int y,
    uint32_t value
);

/**
 * @brief Encode a single tag-tree leaf relative to a threshold.
 *
 * Encodes the leaf value at (x, y) into the packet-header bitstream
 * using the threshold-comparison algorithm from Annex B.10.2.
 * For value v and threshold t, emits min(v, t) zero bits followed
 * by a one bit if v ≤ t (confirming v = t), or no one bit if v > t.
 *
 * @param tree Tag tree with leaf values set.
 * @param x Horizontal leaf index.
 * @param y Vertical leaf index.
 * @param threshold Current threshold value t to compare against.
 * @param header Packet header receiving the encoded bits.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if inputs are NULL or (x, y)
 *         is out of bounds.
 */
dic_status j2k_tagtree_encode_leaf(
    const j2k_tagtree *tree,
    int x,
    int y,
    uint32_t threshold,
    j2k_packet_header *header
);

#ifdef __cplusplus
}
#endif

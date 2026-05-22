#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"
#include "j2k/dic_j2k_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_j2k_tagtree
{
    int width; /**< Number of leaf values in the horizontal direction. */
    int height; /**< Number of leaf values in the vertical direction. */
    uint32_t *values; /**< Leaf values stored in row-major order. */
} dic_j2k_tagtree;

void dic_j2k_tagtree_init(dic_j2k_tagtree *tree);
void dic_j2k_tagtree_free(dic_j2k_tagtree *tree);

dic_status dic_j2k_tagtree_alloc(
    dic_j2k_tagtree *tree,
    int width,
    int height
);

dic_status dic_j2k_tagtree_set(
    dic_j2k_tagtree *tree,
    int x,
    int y,
    uint32_t value
);

dic_status dic_j2k_tagtree_encode_leaf(
    const dic_j2k_tagtree *tree,
    int x,
    int y,
    uint32_t threshold,
    dic_j2k_packet_header *header
);

#ifdef __cplusplus
}
#endif

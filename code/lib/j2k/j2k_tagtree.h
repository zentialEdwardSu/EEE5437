#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"
#include "j2k/j2k_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct j2k_tagtree
{
    int width; /**< Number of leaf values in the horizontal direction. */
    int height; /**< Number of leaf values in the vertical direction. */
    uint32_t *values; /**< Leaf values stored in row-major order. */
} j2k_tagtree;

void j2k_tagtree_init(j2k_tagtree *tree);
void j2k_tagtree_free(j2k_tagtree *tree);

dic_status j2k_tagtree_alloc(
    j2k_tagtree *tree,
    int width,
    int height
);

dic_status j2k_tagtree_set(
    j2k_tagtree *tree,
    int x,
    int y,
    uint32_t value
);

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

#pragma once

#include "list/list.h"

#include <stdlib.h>

typedef struct
{
    int val;
    list_head_t link;
} list_test_node_t;

DEFINE_LIST(node, list_test_node_t, link)

static inline list_test_node_t *list_test_make_node(int val)
{
    list_test_node_t *node = (list_test_node_t *)malloc(sizeof(*node));
    if (node == NULL)
        return NULL;

    node->val = val;
    list_head_init(&node->link);
    return node;
}

static inline void list_test_free_nodes(list_head_t *head)
{
    list_test_node_t *cur;
    list_test_node_t *tmp;

    list_for_each_entry_safe(cur, tmp, head, link)
    {
        list_del(&cur->link);
        free(cur);
    }
}

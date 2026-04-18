#include "pqueue.h"

void pq_init(pqueue_t *q, pq_cmp_fn cmp)
{
    list_head_init(&q->head);
    q->cmp = cmp;
    q->size = 0;
}

bool pq_empty(const pqueue_t *q)
{
    return list_empty(&q->head);
}

size_t pq_size(const pqueue_t *q)
{
    return q->size;
}

void pq_push(pqueue_t *q, pq_node_t *node)
{
    list_head_t *pos;

    /* find first cmp(node, pos_node) >= 0, that is p_node <= p_pos_node */
    list_for_each(pos, &q->head)
    {
        pq_node_t *cur = pq_node_of_list(pos);
        if (q->cmp(node, cur) > 0)
        {
            /* node not better than cur, insert ahead for FIFO */
            list_add_before(&node->list, pos);
            q->size++;
            return;
        }
    }

    list_add_tail(&node->list, &q->head);
    q->size++;
}

pq_node_t *pq_peek(const pqueue_t *q)
{
    if (pq_empty(q))
        return NULL;
    return pq_node_of_list(q->head.next);
}

pq_node_t *pq_pop(pqueue_t *q)
{
    if (pq_empty(q))
        return NULL;
    pq_node_t *top = pq_node_of_list(q->head.next);
    list_del(&top->list);
    q->size--;
    return top;
}

void pq_remove(pqueue_t *q, pq_node_t *node)
{
    list_del(&node->list);
    q->size--;
}

void pq_repush(pqueue_t *q, pq_node_t *node)
{
    pq_remove(q, node);
    pq_push(q, node);
}

void pq_drain(pqueue_t *q, void (*cb)(pq_node_t *node))
{
    list_head_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &q->head)
    {
        pq_node_t *n = pq_node_of_list(pos);
        list_del(pos);
        q->size--;
        if (cb)
            cb(n);
    }
}

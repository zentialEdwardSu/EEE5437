#pragma once

#include "list/list.h"
#include <stddef.h>
#include <stdbool.h>

typedef struct pq_node
{
    list_head_t list;
} pq_node_t;

typedef int (*pq_cmp_fn)(const pq_node_t *a, const pq_node_t *b);

// control block of priority queue
typedef struct
{
    list_head_t head; /* guard, guard->next have highest priority */
    pq_cmp_fn cmp;    /* comparer */
    size_t size;
} pqueue_t;

void pq_init(pqueue_t *q, pq_cmp_fn cmp);
bool pq_empty(const pqueue_t *q);
size_t pq_size(const pqueue_t *q);
void pq_push(pqueue_t *q, pq_node_t *node);
pq_node_t *pq_peek(const pqueue_t *q);
pq_node_t *pq_pop(pqueue_t *q);
void pq_remove(pqueue_t *q, pq_node_t *node);
void pq_repush(pqueue_t *q, pq_node_t *node);
void pq_drain(pqueue_t *q, void (*cb)(pq_node_t *node));

#define pq_node_of_list(ptr) \
    container_of(ptr, pq_node_t, list)

/**
 *   typedef struct { int priority; char name[32]; pq_node_t link; } Job;
 *   DEFINE_PQUEUE(job, Job, link, priority, int)
 *
 *   pqueue_t q;
 *   job_pq_init(&q);
 *   job_pq_push(&q, my_job);
 *   Job *top = job_pq_peek(&q);
 */
#define DEFINE_PQUEUE(prefix, type, pq_member, prio_field, prio_type)  \
                                                                       \
    static inline int prefix##__pq_cmp(const pq_node_t *a,             \
                                       const pq_node_t *b)             \
    {                                                                  \
        const type *ta = container_of(a, type, pq_member);             \
        const type *tb = container_of(b, type, pq_member);             \
        if ((prio_type)(ta->prio_field) < (prio_type)(tb->prio_field)) \
            return 1;                                                  \
        if ((prio_type)(ta->prio_field) > (prio_type)(tb->prio_field)) \
            return -1;                                                 \
        return 0;                                                      \
    }                                                                  \
                                                                       \
    static inline void prefix##_pq_init(pqueue_t *q)                   \
    {                                                                  \
        pq_init(q, prefix##__pq_cmp);                                  \
    }                                                                  \
                                                                       \
    static inline void prefix##_pq_push(pqueue_t *q, type *item)       \
    {                                                                  \
        pq_push(q, &(item)->pq_member);                                \
    }                                                                  \
                                                                       \
    static inline type *prefix##_pq_peek(pqueue_t *q)                  \
    {                                                                  \
        pq_node_t *n = pq_peek(q);                                     \
        return n ? container_of(n, type, pq_member) : NULL;            \
    }                                                                  \
                                                                       \
    static inline type *prefix##_pq_pop(pqueue_t *q)                   \
    {                                                                  \
        pq_node_t *n = pq_pop(q);                                      \
        return n ? container_of(n, type, pq_member) : NULL;            \
    }                                                                  \
                                                                       \
    static inline void prefix##_pq_remove(pqueue_t *q, type *item)     \
    {                                                                  \
        pq_remove(q, &(item)->pq_member);                              \
    }                                                                  \
                                                                       \
    static inline void prefix##_pq_repush(pqueue_t *q, type *item)     \
    {                                                                  \
        pq_repush(q, &(item)->pq_member);                              \
    }                                                                  \
                                                                       \
    static inline size_t prefix##_pq_size(const pqueue_t *q)           \
    {                                                                  \
        return pq_size(q);                                             \
    }                                                                  \
                                                                       \
    static inline bool prefix##_pq_empty(const pqueue_t *q)            \
    {                                                                  \
        return pq_empty(q);                                            \
    }
#define DEFINE_PQUEUE_MAX(prefix, type, pq_member, prio_field, prio_type) \
                                                                          \
    static inline int prefix##__pq_cmp(const pq_node_t *a,                \
                                       const pq_node_t *b)                \
    {                                                                     \
        const type *ta = container_of(a, type, pq_member);                \
        const type *tb = container_of(b, type, pq_member);                \
        if ((prio_type)(ta->prio_field) > (prio_type)(tb->prio_field))    \
            return 1;                                                     \
        if ((prio_type)(ta->prio_field) < (prio_type)(tb->prio_field))    \
            return -1;                                                    \
        return 0;                                                         \
    }                                                                     \
                                                                          \
    static inline void prefix##_pq_init(pqueue_t *q)                      \
    {                                                                     \
        pq_init(q, prefix##__pq_cmp);                                     \
    }                                                                     \
                                                                          \
    static inline void prefix##_pq_push(pqueue_t *q, type *item)          \
    {                                                                     \
        pq_push(q, &(item)->pq_member);                                   \
    }                                                                     \
                                                                          \
    static inline type *prefix##_pq_peek(pqueue_t *q)                     \
    {                                                                     \
        pq_node_t *n = pq_peek(q);                                        \
        return n ? container_of(n, type, pq_member) : NULL;               \
    }                                                                     \
                                                                          \
    static inline type *prefix##_pq_pop(pqueue_t *q)                      \
    {                                                                     \
        pq_node_t *n = pq_pop(q);                                         \
        return n ? container_of(n, type, pq_member) : NULL;               \
    }                                                                     \
                                                                          \
    static inline void prefix##_pq_remove(pqueue_t *q, type *item)        \
    {                                                                     \
        pq_remove(q, &(item)->pq_member);                                 \
    }                                                                     \
                                                                          \
    static inline void prefix##_pq_repush(pqueue_t *q, type *item)        \
    {                                                                     \
        pq_repush(q, &(item)->pq_member);                                 \
    }                                                                     \
                                                                          \
    static inline size_t prefix##_pq_size(const pqueue_t *q)              \
    {                                                                     \
        return pq_size(q);                                                \
    }                                                                     \
                                                                          \
    static inline bool prefix##_pq_empty(const pqueue_t *q)               \
    {                                                                     \
        return pq_empty(q);                                               \
    }

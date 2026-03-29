#pragma once

#include "pqueue/pqueue.h"

#include <stdlib.h>

typedef struct
{
    int priority;
    int value;
    pq_node_t link;
} pqueue_test_job_t;

DEFINE_PQUEUE(job, pqueue_test_job_t, link, priority, int)
DEFINE_PQUEUE_MAX(job_max, pqueue_test_job_t, link, priority, int)

static inline pqueue_test_job_t *pqueue_test_make_job(int priority, int value)
{
    pqueue_test_job_t *job = (pqueue_test_job_t *)malloc(sizeof(*job));
    if (job == NULL)
        return NULL;

    job->priority = priority;
    job->value = value;
    list_head_init(&job->link.list);
    return job;
}

static inline void pqueue_test_free_job(pqueue_test_job_t *job)
{
    free(job);
}

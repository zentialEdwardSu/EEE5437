#include "pqueue_test_utils.h"
#include "test_helpers.h"

static int g_drained_count = 0;

static void count_drain(pq_node_t *node)
{
    pqueue_test_job_t *job = container_of(node, pqueue_test_job_t, link);
    ++g_drained_count;
    pqueue_test_free_job(job);
}

int main(void)
{
    pqueue_t queue;
    pqueue_test_job_t *a;
    pqueue_test_job_t *b;
    pqueue_test_job_t *c;

    job_pq_init(&queue);

    a = pqueue_test_make_job(10, 1);
    b = pqueue_test_make_job(20, 2);
    c = pqueue_test_make_job(30, 3);

    DIC_EXPECT(a != NULL);
    DIC_EXPECT(b != NULL);
    DIC_EXPECT(c != NULL);

    job_pq_push(&queue, a);
    job_pq_push(&queue, b);
    job_pq_push(&queue, c);

    g_drained_count = 0;
    pq_drain(&queue, count_drain);

    DIC_EXPECT(g_drained_count == 3);
    DIC_EXPECT(job_pq_empty(&queue));
    DIC_EXPECT(job_pq_size(&queue) == 0);

    return 0;
}

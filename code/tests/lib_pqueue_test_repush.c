#include "pqueue_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    pqueue_t queue;
    pqueue_test_job_t *a;
    pqueue_test_job_t *b;

    job_pq_init(&queue);

    a = pqueue_test_make_job(30, 1);
    b = pqueue_test_make_job(20, 2);

    DIC_EXPECT(a != NULL);
    DIC_EXPECT(b != NULL);

    job_pq_push(&queue, a);
    job_pq_push(&queue, b);
    DIC_EXPECT(job_pq_peek(&queue) == b);

    a->priority = 5;
    job_pq_repush(&queue, a);

    DIC_EXPECT(job_pq_peek(&queue) == a);
    DIC_EXPECT(job_pq_pop(&queue) == a);
    DIC_EXPECT(job_pq_pop(&queue) == b);

    pqueue_test_free_job(a);
    pqueue_test_free_job(b);
    return 0;
}

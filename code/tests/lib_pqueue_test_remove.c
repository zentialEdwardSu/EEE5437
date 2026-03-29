#include "pqueue_test_utils.h"
#include "test_helpers.h"

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

    pq_remove(&queue, &b->link);
    DIC_EXPECT(job_pq_size(&queue) == 2);
    DIC_EXPECT(job_pq_peek(&queue) == a);
    DIC_EXPECT(job_pq_pop(&queue) == a);
    DIC_EXPECT(job_pq_pop(&queue) == c);
    DIC_EXPECT(job_pq_empty(&queue));

    pqueue_test_free_job(a);
    pqueue_test_free_job(b);
    pqueue_test_free_job(c);
    return 0;
}

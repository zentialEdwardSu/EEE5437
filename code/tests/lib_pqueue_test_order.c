#include "pqueue_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    pqueue_t queue;
    pqueue_test_job_t *a;
    pqueue_test_job_t *b;
    pqueue_test_job_t *c;
    pqueue_test_job_t *top;

    job_pq_init(&queue);

    a = pqueue_test_make_job(30, 1);
    b = pqueue_test_make_job(10, 2);
    c = pqueue_test_make_job(20, 3);

    DIC_EXPECT(a != NULL);
    DIC_EXPECT(b != NULL);
    DIC_EXPECT(c != NULL);

    job_pq_push(&queue, a);
    job_pq_push(&queue, b);
    job_pq_push(&queue, c);

    DIC_EXPECT(job_pq_size(&queue) == 3);
    DIC_EXPECT(job_pq_peek(&queue)->priority == 10);

    top = job_pq_pop(&queue);
    DIC_EXPECT(top == b);
    pqueue_test_free_job(top);

    top = job_pq_pop(&queue);
    DIC_EXPECT(top == c);
    pqueue_test_free_job(top);

    top = job_pq_pop(&queue);
    DIC_EXPECT(top == a);
    pqueue_test_free_job(top);

    DIC_EXPECT(job_pq_empty(&queue));
    return 0;
}

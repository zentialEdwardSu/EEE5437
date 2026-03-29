#include "pqueue_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    pqueue_t queue;
    pqueue_test_job_t *a;
    pqueue_test_job_t *b;
    pqueue_test_job_t *top;

    job_max_pq_init(&queue);

    a = pqueue_test_make_job(10, 1);
    b = pqueue_test_make_job(30, 2);

    DIC_EXPECT(a != NULL);
    DIC_EXPECT(b != NULL);

    job_max_pq_push(&queue, a);
    job_max_pq_push(&queue, b);

    top = job_max_pq_peek(&queue);
    DIC_EXPECT(top == b);
    DIC_EXPECT(top->priority == 30);
    DIC_EXPECT(job_max_pq_size(&queue) == 2);
    DIC_EXPECT(!job_max_pq_empty(&queue));

    top = job_max_pq_pop(&queue);
    DIC_EXPECT(top == b);
    pqueue_test_free_job(top);

    top = job_max_pq_pop(&queue);
    DIC_EXPECT(top == a);
    pqueue_test_free_job(top);

    DIC_EXPECT(job_max_pq_empty(&queue));
    return 0;
}

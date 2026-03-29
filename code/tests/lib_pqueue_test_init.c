#include "pqueue_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    pqueue_t queue;

    job_pq_init(&queue);

    DIC_EXPECT(pq_empty(&queue));
    DIC_EXPECT(job_pq_empty(&queue));
    DIC_EXPECT(pq_size(&queue) == 0);
    DIC_EXPECT(job_pq_size(&queue) == 0);
    DIC_EXPECT(pq_peek(&queue) == NULL);
    DIC_EXPECT(pq_pop(&queue) == NULL);

    return 0;
}

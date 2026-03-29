#include "list_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    list_head_t head;

    list_head_init(&head);

    DIC_EXPECT(list_empty(&head));
    DIC_EXPECT(head.next == &head);
    DIC_EXPECT(head.prev == &head);
    DIC_EXPECT(list_count(&head) == 0);

    return 0;
}

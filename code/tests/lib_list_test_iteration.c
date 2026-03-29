#include "list_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    list_head_t head;
    list_test_node_t *cur;
    list_test_node_t *tmp;
    int values[] = {10, 20, 30, 40, 50};
    int idx;

    list_head_init(&head);

    for (idx = 0; idx < 5; ++idx)
    {
        list_test_node_t *node = list_test_make_node(values[idx]);
        DIC_EXPECT(node != NULL);
        list_add_tail(&node->link, &head);
    }

    idx = 0;
    list_for_each_entry(cur, &head, link)
    {
        DIC_EXPECT(cur->val == values[idx]);
        ++idx;
    }
    DIC_EXPECT(idx == 5);

    list_for_each_entry_safe(cur, tmp, &head, link)
    {
        if (cur->val % 20 == 0)
        {
            list_del(&cur->link);
            free(cur);
        }
    }

    DIC_EXPECT(list_count(&head) == 3);
    DIC_EXPECT(list_first_entry(&head, list_test_node_t, link)->val == 10);
    DIC_EXPECT(list_last_entry(&head, list_test_node_t, link)->val == 50);

    list_test_free_nodes(&head);
    return 0;
}

#include "list_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    list_head_t head;
    list_test_node_t *n1;
    list_test_node_t *n2;
    list_test_node_t *n3;
    list_test_node_t *it;

    node_list_init(&head);
    DIC_EXPECT(node_list_empty(&head));

    n1 = list_test_make_node(100);
    n2 = list_test_make_node(200);
    n3 = list_test_make_node(300);

    DIC_EXPECT(n1 != NULL);
    DIC_EXPECT(n2 != NULL);
    DIC_EXPECT(n3 != NULL);

    node_list_add_tail(n1, &head);
    node_list_add_tail(n2, &head);
    node_list_add_tail(n3, &head);

    DIC_EXPECT(node_list_count(&head) == 3);
    DIC_EXPECT(node_list_first(&head)->val == 100);
    DIC_EXPECT(node_list_last(&head)->val == 300);

    it = node_list_first(&head);
    DIC_EXPECT(it->val == 100);
    it = node_list_next(it, &head);
    DIC_EXPECT(it->val == 200);
    it = node_list_next(it, &head);
    DIC_EXPECT(it->val == 300);
    it = node_list_next(it, &head);
    DIC_EXPECT(it == NULL);

    node_list_del(n2);
    free(n2);
    DIC_EXPECT(node_list_count(&head) == 2);

    list_test_free_nodes(&head);
    return 0;
}

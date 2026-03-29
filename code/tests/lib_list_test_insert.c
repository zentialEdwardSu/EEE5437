#include "list_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    list_head_t head;
    list_test_node_t *a;
    list_test_node_t *b;
    list_test_node_t *c;
    list_test_node_t *z;

    list_head_init(&head);

    a = list_test_make_node(1);
    b = list_test_make_node(2);
    c = list_test_make_node(3);
    z = list_test_make_node(0);

    DIC_EXPECT(a != NULL);
    DIC_EXPECT(b != NULL);
    DIC_EXPECT(c != NULL);
    DIC_EXPECT(z != NULL);

    list_add_tail(&a->link, &head);
    list_add_tail(&b->link, &head);
    list_add_tail(&c->link, &head);

    DIC_EXPECT(!list_empty(&head));
    DIC_EXPECT(list_count(&head) == 3);
    DIC_EXPECT(list_first_entry(&head, list_test_node_t, link)->val == 1);
    DIC_EXPECT(list_last_entry(&head, list_test_node_t, link)->val == 3);

    list_add(&z->link, &head);
    DIC_EXPECT(list_count(&head) == 4);
    DIC_EXPECT(list_first_entry(&head, list_test_node_t, link)->val == 0);

    list_test_free_nodes(&head);
    return 0;
}

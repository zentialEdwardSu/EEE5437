#include "list_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    list_head_t head;
    list_test_node_t *a;
    list_test_node_t *b;
    list_test_node_t *c;

    list_head_init(&head);

    a = list_test_make_node(10);
    b = list_test_make_node(20);
    c = list_test_make_node(30);

    DIC_EXPECT(a != NULL);
    DIC_EXPECT(b != NULL);
    DIC_EXPECT(c != NULL);

    list_add_tail(&a->link, &head);
    list_add_tail(&b->link, &head);
    list_add_tail(&c->link, &head);

    list_del(&b->link);
    DIC_EXPECT(list_count(&head) == 2);
    DIC_EXPECT(b->link.next == &b->link);
    DIC_EXPECT(b->link.prev == &b->link);
    free(b);

    list_del(&a->link);
    DIC_EXPECT(list_first_entry(&head, list_test_node_t, link)->val == 30);
    free(a);

    list_del(&c->link);
    DIC_EXPECT(list_empty(&head));
    DIC_EXPECT(list_count(&head) == 0);
    free(c);

    return 0;
}

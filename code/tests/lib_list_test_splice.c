#include "list_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    list_head_t first;
    list_head_t second;
    list_test_node_t *a;
    list_test_node_t *b;
    list_test_node_t *c;
    list_test_node_t *d;

    list_head_init(&first);
    list_head_init(&second);

    a = list_test_make_node(1);
    b = list_test_make_node(2);
    c = list_test_make_node(3);
    d = list_test_make_node(4);

    DIC_EXPECT(a != NULL);
    DIC_EXPECT(b != NULL);
    DIC_EXPECT(c != NULL);
    DIC_EXPECT(d != NULL);

    list_add_tail(&a->link, &first);
    list_add_tail(&b->link, &first);
    list_add_tail(&c->link, &second);
    list_add_tail(&d->link, &second);

    list_splice(&second, &first);

    DIC_EXPECT(list_count(&first) == 4);
    DIC_EXPECT(list_empty(&second));
    DIC_EXPECT(list_first_entry(&first, list_test_node_t, link)->val == 3);
    DIC_EXPECT(list_last_entry(&first, list_test_node_t, link)->val == 2);

    list_test_free_nodes(&first);
    return 0;
}

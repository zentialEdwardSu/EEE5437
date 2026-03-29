#include "list_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    list_head_t from;
    list_head_t to;
    list_test_node_t *a;
    list_test_node_t *b;
    list_test_node_t *c;

    list_head_init(&from);
    list_head_init(&to);

    a = list_test_make_node(1);
    b = list_test_make_node(2);
    c = list_test_make_node(3);

    DIC_EXPECT(a != NULL);
    DIC_EXPECT(b != NULL);
    DIC_EXPECT(c != NULL);

    list_add_tail(&a->link, &from);
    list_add_tail(&b->link, &from);
    list_add_tail(&c->link, &from);

    list_move(&b->link, &to);
    DIC_EXPECT(list_count(&from) == 2);
    DIC_EXPECT(list_count(&to) == 1);
    DIC_EXPECT(list_first_entry(&to, list_test_node_t, link)->val == 2);

    list_move_tail(&a->link, &to);
    DIC_EXPECT(list_count(&from) == 1);
    DIC_EXPECT(list_last_entry(&to, list_test_node_t, link)->val == 1);

    list_test_free_nodes(&from);
    list_test_free_nodes(&to);
    return 0;
}

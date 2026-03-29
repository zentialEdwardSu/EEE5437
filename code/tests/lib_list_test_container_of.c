#include "list_test_utils.h"
#include "test_helpers.h"

int main(void)
{
    list_test_node_t node;
    list_head_t *link_ptr;
    list_test_node_t *recovered;

    node.val = 999;
    list_head_init(&node.link);

    link_ptr = &node.link;
    recovered = container_of(link_ptr, list_test_node_t, link);

    DIC_EXPECT(recovered == &node);
    DIC_EXPECT(recovered->val == 999);

    return 0;
}

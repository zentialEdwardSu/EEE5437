#include "list.h"

// create a list head that point to itself
void list_head_init(list_head_t *head)
{
    head->next = head;
    head->prev = head;
}

bool list_empty(const list_head_t *head)
{
    return head->next == head;
}

// insert a new node between prev and next
static inline void __list_add(list_head_t *new_node,
                              list_head_t *prev,
                              list_head_t *next)
{
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
}

void list_add(list_head_t *new_node, list_head_t *head)
{
    __list_add(new_node, head, head->next);
}

void list_add_before(list_head_t *new_node, list_head_t *pos)
{
    __list_add(new_node, pos->prev, pos);
}

void list_add_tail(list_head_t *new_node, list_head_t *head)
{
    __list_add(new_node, head->prev, head);
}

static inline void __list_del(list_head_t *prev, list_head_t *next)
{
    next->prev = prev;
    prev->next = next;
}

// remove node
void list_del(list_head_t *entry)
{
    __list_del(entry->prev, entry->next);
    list_head_init(entry);
}

// move entry from origin list, and insert ahead of head
void list_move(list_head_t *entry, list_head_t *head)
{
    list_del(entry);
    list_add(entry, head);
}

// move entry from origin list, and insert to the end of end
void list_move_tail(list_head_t *entry, list_head_t *head)
{
    list_del(entry);
    list_add_tail(entry, head);
}

void list_splice(list_head_t *list, list_head_t *head)
{
    if (list_empty(list))
        return;

    list_head_t *first = list->next;
    list_head_t *last = list->prev;
    list_head_t *at = head->next;

    first->prev = head;
    head->next = first;
    last->next = at;
    at->prev = last;

    list_head_init(list);
}

size_t list_count(const list_head_t *head)
{
    size_t n = 0;
    const list_head_t *cur = head->next;
    while (cur != head)
    {
        ++n;
        cur = cur->next;
    }
    return n;
}

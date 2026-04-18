#pragma once

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

// container_of are used to find parent struct from list head
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

typedef struct list_head
{
    struct list_head *next;
    struct list_head *prev;
} list_head_t;

void list_head_init(list_head_t *head);
bool list_empty(const list_head_t *head);
void list_add(list_head_t *new_node, list_head_t *head);
void list_add_before(list_head_t *new_node, list_head_t *pos);
void list_add_tail(list_head_t *new_node, list_head_t *head);
void list_del(list_head_t *entry);
void list_move(list_head_t *entry, list_head_t *head);
void list_move_tail(list_head_t *entry, list_head_t *head);
void list_splice(list_head_t *list, list_head_t *head);
size_t list_count(const list_head_t *head);

#define list_for_each(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)
 
/** iter safely, pos can be removed */
#define list_for_each_safe(pos, n, head) \
    for ((pos) = (head)->next, (n) = (pos)->next; \
         (pos) != (head); \
         (pos) = (n), (n) = (pos)->next)

#define list_for_each_entry(pos, head, member) \
    for ((pos) = container_of((head)->next, __typeof__(*(pos)), member); \
         &(pos)->member != (head); \
         (pos) = container_of((pos)->member.next, __typeof__(*(pos)), member))
 
/** iter safely, pos can be removed */
#define list_for_each_entry_safe(pos, tmp, head, member) \
    for ((pos) = container_of((head)->next, __typeof__(*(pos)), member), \
         (tmp) = container_of((pos)->member.next, __typeof__(*(pos)), member); \
         &(pos)->member != (head); \
         (pos) = (tmp), \
         (tmp) = container_of((tmp)->member.next, __typeof__(*(tmp)), member))
 
#define list_first_entry(head, type, member) \
    container_of((head)->next, type, member)
 
#define list_last_entry(head, type, member) \
    container_of((head)->prev, type, member)
#define DEFINE_LIST(prefix, type, member)                                    \
                                                                             \
    static inline void prefix##_list_init(list_head_t *head)                 \
    {                                                                        \
        list_head_init(head);                                                \
    }                                                                        \
                                                                             \
    static inline void prefix##_list_add(type *item, list_head_t *head)      \
    {                                                                        \
        list_add(&(item)->member, head);                                     \
    }                                                                        \
                                                                             \
    static inline void prefix##_list_add_tail(type *item, list_head_t *head) \
    {                                                                        \
        list_add_tail(&(item)->member, head);                                \
    }                                                                        \
                                                                             \
    static inline void prefix##_list_del(type *item)                         \
    {                                                                        \
        list_del(&(item)->member);                                           \
    }                                                                        \
                                                                             \
    static inline bool prefix##_list_empty(const list_head_t *head)          \
    {                                                                        \
        return list_empty(head);                                             \
    }                                                                        \
                                                                             \
    static inline size_t prefix##_list_count(const list_head_t *head)        \
    {                                                                        \
        return list_count(head);                                             \
    }                                                                        \
                                                                             \
    static inline type *prefix##_list_first(list_head_t *head)               \
    {                                                                        \
        return list_empty(head)                                              \
                   ? NULL                                                    \
                   : list_first_entry(head, type, member);                   \
    }                                                                        \
                                                                             \
    static inline type *prefix##_list_last(list_head_t *head)                \
    {                                                                        \
        return list_empty(head)                                              \
                   ? NULL                                                    \
                   : list_last_entry(head, type, member);                    \
    }                                                                        \
                                                                             \
    static inline type *prefix##_list_next(type *item, list_head_t *head)    \
    {                                                                        \
        list_head_t *nxt = (item)->member.next;                              \
        return (nxt == head) ? NULL : container_of(nxt, type, member);       \
    }

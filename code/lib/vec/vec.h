#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "errors/errors.h"

static inline dic_status dic_vec_reserve_storage(void** items,
                                                 size_t* capacity,
                                                 size_t element_size,
                                                 size_t min_capacity,
                                                 size_t initial_capacity) {
    void* tmp;

    if (items == NULL || capacity == NULL || element_size == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (min_capacity <= *capacity) return DIC_STATUS_OK;

    size_t cap = *capacity == 0u ? initial_capacity : *capacity;
    if (cap == 0u) cap = 1u;
    while (cap < min_capacity) {
        if (cap > SIZE_MAX / 2u) {
            cap = min_capacity;
            break;
        }
        cap *= 2u;
    }
    if (cap > SIZE_MAX / element_size) return DIC_STATUS_MEMORY_ERROR;

    tmp = realloc(*items, cap * element_size);
    if (tmp == NULL) return DIC_STATUS_MEMORY_ERROR;
    *items = tmp;
    *capacity = cap;
    return DIC_STATUS_OK;
}

#define DEFINE_VEC(prefix, vec_type, element_type, items_member, init_capacity) \
    static inline void prefix##_init(vec_type* vec) {                          \
        if (vec == NULL) return;                                                \
        vec->items_member = NULL;                                               \
        vec->count = 0u;                                                        \
        vec->capacity = 0u;                                                     \
    }                                                                           \
                                                                                \
    static inline void prefix##_free(vec_type* vec) {                           \
        if (vec == NULL) return;                                                \
        free(vec->items_member);                                                \
        prefix##_init(vec);                                                     \
    }                                                                           \
                                                                                \
    static inline dic_status prefix##_reserve(vec_type* vec,                    \
                                              size_t min_capacity) {            \
        if (vec == NULL) return DIC_STATUS_INVALID_ARGUMENT;                    \
        return dic_vec_reserve_storage(                                         \
            (void**)&vec->items_member, &vec->capacity,                         \
            sizeof(vec->items_member[0]), min_capacity, (init_capacity));       \
    }                                                                           \
                                                                                \
    static inline dic_status prefix##_append(vec_type* vec,                     \
                                             element_type value) {              \
        dic_status status;                                                      \
        if (vec == NULL) return DIC_STATUS_INVALID_ARGUMENT;                    \
        if (vec->count == SIZE_MAX) return DIC_STATUS_MEMORY_ERROR;             \
        status = prefix##_reserve(vec, vec->count + 1u);                        \
        if (status != DIC_STATUS_OK) return status;                             \
        vec->items_member[vec->count] = value;                                  \
        ++vec->count;                                                           \
        return DIC_STATUS_OK;                                                   \
    }

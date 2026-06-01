#include "net/net_buffer.h"

/**
 * Implements net's internal bounded FIFO buffer with wrap-around indexing.
 */

#include <stdlib.h>
#include <string.h>

void net_buffer_init(net_buffer *buffer)
{
    if (buffer == NULL)
        return;

    buffer->data = NULL;
    buffer->capacity = 0u;
    buffer->start = 0u;
    buffer->length = 0u;
}

dic_status net_buffer_alloc(net_buffer *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    net_buffer_free(buffer);
    buffer->data = (uint8_t *)malloc(capacity);
    if (buffer->data == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    buffer->capacity = capacity;
    buffer->start = 0u;
    buffer->length = 0u;
    return DIC_STATUS_OK;
}

void net_buffer_free(net_buffer *buffer)
{
    if (buffer == NULL)
        return;

    free(buffer->data);
    net_buffer_init(buffer);
}

dic_status net_buffer_push(net_buffer *buffer, const uint8_t *data, size_t size)
{
    size_t write_index;
    size_t first_count;

    if (buffer == NULL || data == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (size > net_buffer_free_space(buffer))
        return DIC_STATUS_IO_ERROR;
    if (size == 0u)
        return DIC_STATUS_OK;

    write_index = (buffer->start + buffer->length) % buffer->capacity;
    first_count = buffer->capacity - write_index;
    if (first_count > size)
        first_count = size;

    memcpy(buffer->data + write_index, data, first_count);
    memcpy(buffer->data, data + first_count, size - first_count);
    buffer->length += size;
    return DIC_STATUS_OK;
}

size_t net_buffer_pop(net_buffer *buffer, uint8_t *data, size_t capacity)
{
    size_t count;
    size_t first_count;

    if (buffer == NULL || data == NULL || capacity == 0u)
        return 0u;

    count = buffer->length;
    if (count > capacity)
        count = capacity;
    if (count == 0u)
        return 0u;

    first_count = buffer->capacity - buffer->start;
    if (first_count > count)
        first_count = count;

    memcpy(data, buffer->data + buffer->start, first_count);
    memcpy(data + first_count, buffer->data, count - first_count);
    buffer->start = (buffer->start + count) % buffer->capacity;
    buffer->length -= count;
    if (buffer->length == 0u)
        buffer->start = 0u;

    return count;
}

size_t net_buffer_free_space(const net_buffer *buffer)
{
    if (buffer == NULL || buffer->data == NULL || buffer->capacity < buffer->length)
        return 0u;

    return buffer->capacity - buffer->length;
}

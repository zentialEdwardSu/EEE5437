#pragma once

/**
 * Internal bounded FIFO buffer used by net_receive to preserve bytes that
 * have arrived from the transport but have not yet been consumed by callers.
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

typedef struct net_buffer
{
    uint8_t *data;
    size_t capacity;
    size_t start;
    size_t length;
} net_buffer;

/**
 * Initializes an empty buffer object.
 */
void net_buffer_init(net_buffer *buffer);

/**
 * Allocates storage for a bounded FIFO buffer.
 */
dic_status net_buffer_alloc(net_buffer *buffer, size_t capacity);

/**
 * Releases buffer storage and resets the buffer to empty.
 */
void net_buffer_free(net_buffer *buffer);

/**
 * Appends bytes to the buffer, failing if the bounded capacity is exceeded.
 */
dic_status net_buffer_push(net_buffer *buffer, const uint8_t *data, size_t size);

/**
 * Removes up to capacity bytes from the buffer into the caller's destination.
 */
size_t net_buffer_pop(net_buffer *buffer, uint8_t *data, size_t capacity);

/**
 * Returns the unused byte capacity of the buffer.
 */
size_t net_buffer_free_space(const net_buffer *buffer);

#pragma once

/**
 * Internal bounded FIFO buffer used by libnet_receive to preserve bytes that
 * have arrived from the transport but have not yet been consumed by callers.
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

typedef struct libnet_buffer
{
    uint8_t *data;
    size_t capacity;
    size_t start;
    size_t length;
} libnet_buffer;

/**
 * Initializes an empty buffer object.
 */
void libnet_buffer_init(libnet_buffer *buffer);

/**
 * Allocates storage for a bounded FIFO buffer.
 */
dic_status libnet_buffer_alloc(libnet_buffer *buffer, size_t capacity);

/**
 * Releases buffer storage and resets the buffer to empty.
 */
void libnet_buffer_free(libnet_buffer *buffer);

/**
 * Appends bytes to the buffer, failing if the bounded capacity is exceeded.
 */
dic_status libnet_buffer_push(libnet_buffer *buffer, const uint8_t *data, size_t size);

/**
 * Removes up to capacity bytes from the buffer into the caller's destination.
 */
size_t libnet_buffer_pop(libnet_buffer *buffer, uint8_t *data, size_t capacity);

/**
 * Returns the unused byte capacity of the buffer.
 */
size_t libnet_buffer_free_space(const libnet_buffer *buffer);

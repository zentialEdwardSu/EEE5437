#include "libnet/libnet.h"

/**
 * Implements libnet's control handle, file transport, UDP transport, throttling,
 * packet loss, and receive buffering.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libnet/libnet_buffer.h"
#include "libnet/libnet_platform.h"

#define LIBNET_DEFAULT_BUFFER_CAPACITY 65536u
#define LIBNET_PORT_CHUNK_SIZE 1200u

struct libnet_control
{
    libnet_config config;
    libnet_buffer buffer;
    FILE *file;
    long file_read_offset;
    long file_write_offset;
    libnet_socket socket_handle;
    libnet_socket_address peer_address;
    int has_peer;
    uint16_t bound_port;
    uint32_t random_state;
};

void libnet_config_init(libnet_config *config)
{
    if (config == NULL)
        return;

    config->transport = LIBNET_TRANSPORT_FILE;
    config->file_path = NULL;
    config->host = "127.0.0.1";
    config->bind_port = 0u;
    config->peer_port = 0u;
    config->buffer_capacity = LIBNET_DEFAULT_BUFFER_CAPACITY;
    config->bytes_per_second = 0u;
    config->loss_probability = 0.0;
    config->random_seed = 1u;
}

static dic_status libnet_validate_config(const libnet_config *config)
{
    if (config == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (config->buffer_capacity == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (config->loss_probability < 0.0 || config->loss_probability > 1.0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (config->transport == LIBNET_TRANSPORT_FILE)
    {
        if (config->file_path == NULL)
            return DIC_STATUS_INVALID_ARGUMENT;
        return DIC_STATUS_OK;
    }
    if (config->transport == LIBNET_TRANSPORT_PORT)
        return DIC_STATUS_OK;

    return DIC_STATUS_INVALID_ARGUMENT;
}

static uint32_t libnet_random_next(libnet_control *control)
{
    uint32_t value = control->random_state;

    if (value == 0u)
        value = 1u;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    control->random_state = value;
    return value;
}

static int libnet_should_drop(libnet_control *control)
{
    double sample;

    if (control->config.loss_probability <= 0.0)
        return 0;
    if (control->config.loss_probability >= 1.0)
        return 1;

    sample = (double)libnet_random_next(control) / 4294967295.0;
    return sample < control->config.loss_probability;
}

static void libnet_throttle(libnet_control *control, size_t byte_count)
{
    uint64_t milliseconds;

    if (control->config.bytes_per_second == 0u || byte_count == 0u)
        return;

    milliseconds = ((uint64_t)byte_count * 1000u) / control->config.bytes_per_second;
    if ((((uint64_t)byte_count * 1000u) % control->config.bytes_per_second) != 0u)
        ++milliseconds;
    if (milliseconds > 0xffffffffu)
        milliseconds = 0xffffffffu;

    libnet_platform_sleep_ms((uint32_t)milliseconds);
}

static dic_status libnet_open_file_transport(libnet_control *control)
{
    control->file = libnet_platform_open_file(control->config.file_path, "w+b");
    if (control->file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;

    control->file_read_offset = 0L;
    control->file_write_offset = 0L;
    return DIC_STATUS_OK;
}

static dic_status libnet_open_port_transport(libnet_control *control)
{
    dic_status status;

    status = libnet_socket_startup();
    if (status != DIC_STATUS_OK)
        return status;

    status = libnet_socket_open_udp(&control->socket_handle, control->config.bind_port);
    if (status != DIC_STATUS_OK)
    {
        libnet_socket_cleanup();
        return status;
    }

    status = libnet_socket_bound_port(control->socket_handle, &control->bound_port);
    if (status != DIC_STATUS_OK)
        return status;

    if (control->config.peer_port != 0u)
    {
        status = libnet_socket_address_ipv4(
            control->config.host,
            control->config.peer_port,
            &control->peer_address
        );
        if (status != DIC_STATUS_OK)
            return status;
        control->has_peer = 1;
    }

    return DIC_STATUS_OK;
}

dic_status libnet_control_open(libnet_control **control, const libnet_config *config)
{
    libnet_control *opened;
    dic_status status;

    if (control == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    *control = NULL;

    status = libnet_validate_config(config);
    if (status != DIC_STATUS_OK)
        return status;

    opened = (libnet_control *)calloc(1u, sizeof(*opened));
    if (opened == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    opened->config = *config;
    opened->socket_handle = LIBNET_INVALID_SOCKET;
    opened->random_state = config->random_seed == 0u ? 1u : config->random_seed;
    libnet_buffer_init(&opened->buffer);

    status = libnet_buffer_alloc(&opened->buffer, config->buffer_capacity);
    if (status == DIC_STATUS_OK && config->transport == LIBNET_TRANSPORT_FILE)
        status = libnet_open_file_transport(opened);
    if (status == DIC_STATUS_OK && config->transport == LIBNET_TRANSPORT_PORT)
        status = libnet_open_port_transport(opened);

    if (status != DIC_STATUS_OK)
    {
        libnet_control_close(opened);
        return status;
    }

    *control = opened;
    return DIC_STATUS_OK;
}

void libnet_control_close(libnet_control *control)
{
    if (control == NULL)
        return;

    if (control->file != NULL)
        fclose(control->file);
    if (control->socket_handle != LIBNET_INVALID_SOCKET)
    {
        libnet_socket_close(control->socket_handle);
        libnet_socket_cleanup();
    }

    libnet_buffer_free(&control->buffer);
    free(control);
}

uint16_t libnet_control_port(const libnet_control *control)
{
    if (control == NULL || control->config.transport != LIBNET_TRANSPORT_PORT)
        return 0u;

    return control->bound_port;
}

size_t libnet_control_buffered_bytes(const libnet_control *control)
{
    if (control == NULL)
        return 0u;

    return control->buffer.length;
}

dic_status libnet_control_set_rate(libnet_control *control, uint32_t bytes_per_second)
{
    if (control == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    control->config.bytes_per_second = bytes_per_second;
    return DIC_STATUS_OK;
}

dic_status libnet_control_set_loss(
    libnet_control *control,
    double loss_probability,
    uint32_t random_seed
)
{
    if (control == NULL || loss_probability < 0.0 || loss_probability > 1.0)
        return DIC_STATUS_INVALID_ARGUMENT;

    control->config.loss_probability = loss_probability;
    control->random_state = random_seed == 0u ? 1u : random_seed;
    return DIC_STATUS_OK;
}

static dic_status libnet_send_file(libnet_control *control, const uint8_t *data, size_t size)
{
    if (control->file == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (libnet_should_drop(control))
        return DIC_STATUS_OK;

    if (fseek(control->file, control->file_write_offset, SEEK_SET) != 0)
        return DIC_STATUS_IO_ERROR;
    if (fwrite(data, 1u, size, control->file) != size)
        return DIC_STATUS_IO_ERROR;
    if (fflush(control->file) != 0)
        return DIC_STATUS_IO_ERROR;

    control->file_write_offset += (long)size;
    return DIC_STATUS_OK;
}

static dic_status libnet_send_port(libnet_control *control, const uint8_t *data, size_t size)
{
    size_t offset = 0u;

    if (!control->has_peer)
        return DIC_STATUS_INVALID_ARGUMENT;

    while (offset < size)
    {
        size_t chunk_size = size - offset;
        dic_status status;

        if (chunk_size > LIBNET_PORT_CHUNK_SIZE)
            chunk_size = LIBNET_PORT_CHUNK_SIZE;

        if (!libnet_should_drop(control))
        {
            status = libnet_socket_send_to(
                control->socket_handle,
                &control->peer_address,
                data + offset,
                chunk_size
            );
            if (status != DIC_STATUS_OK)
                return status;
        }

        offset += chunk_size;
    }

    return DIC_STATUS_OK;
}

dic_status libnet_send(
    libnet_control *control,
    const void *data,
    size_t size,
    size_t *sent
)
{
    dic_status status;

    if (sent != NULL)
        *sent = 0u;
    if (control == NULL || data == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (size == 0u)
        return DIC_STATUS_OK;

    libnet_throttle(control, size);

    if (control->config.transport == LIBNET_TRANSPORT_FILE)
        status = libnet_send_file(control, (const uint8_t *)data, size);
    else
        status = libnet_send_port(control, (const uint8_t *)data, size);

    if (status == DIC_STATUS_OK && sent != NULL)
        *sent = size;

    return status;
}

static dic_status libnet_drain_file(libnet_control *control)
{
    uint8_t temp[1024];

    if (control->file == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    while (libnet_buffer_free_space(&control->buffer) > 0u)
    {
        size_t read_capacity = libnet_buffer_free_space(&control->buffer);
        size_t read_count;

        if (read_capacity > sizeof(temp))
            read_capacity = sizeof(temp);
        if (fseek(control->file, control->file_read_offset, SEEK_SET) != 0)
            return DIC_STATUS_IO_ERROR;

        read_count = fread(temp, 1u, read_capacity, control->file);
        if (read_count == 0u)
        {
            if (ferror(control->file) != 0)
                return DIC_STATUS_FILE_READ_ERROR;
            return DIC_STATUS_OK;
        }

        if (libnet_buffer_push(&control->buffer, temp, read_count) != DIC_STATUS_OK)
            return DIC_STATUS_IO_ERROR;
        control->file_read_offset += (long)read_count;
    }

    return DIC_STATUS_OK;
}

static dic_status libnet_drain_port(libnet_control *control)
{
    uint8_t temp[LIBNET_PORT_CHUNK_SIZE];

    while (libnet_buffer_free_space(&control->buffer) > 0u)
    {
        size_t received = 0u;
        dic_status status;

        status = libnet_socket_receive_available(
            control->socket_handle,
            temp,
            sizeof(temp),
            &received
        );
        if (status != DIC_STATUS_OK)
            return status;
        if (received == 0u)
            return DIC_STATUS_OK;
        if (received > libnet_buffer_free_space(&control->buffer))
            return DIC_STATUS_IO_ERROR;

        status = libnet_buffer_push(&control->buffer, temp, received);
        if (status != DIC_STATUS_OK)
            return status;
    }

    return DIC_STATUS_OK;
}

dic_status libnet_receive(
    libnet_control *control,
    void *data,
    size_t capacity,
    size_t *received
)
{
    dic_status status;

    if (received != NULL)
        *received = 0u;
    if (control == NULL || data == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (capacity == 0u)
        return DIC_STATUS_OK;

    if (control->config.transport == LIBNET_TRANSPORT_FILE)
        status = libnet_drain_file(control);
    else
        status = libnet_drain_port(control);
    if (status != DIC_STATUS_OK)
        return status;

    if (received != NULL)
        *received = libnet_buffer_pop(&control->buffer, (uint8_t *)data, capacity);
    else
        (void)libnet_buffer_pop(&control->buffer, (uint8_t *)data, capacity);

    return DIC_STATUS_OK;
}

#include "net/net.h"

/**
 * Implements net's control handle, file transport, UDP transport, throttling,
 * packet loss, and receive buffering.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/net_buffer.h"
#include "net/net_platform.h"

#define net_DEFAULT_BUFFER_CAPACITY 65536u
#define net_PORT_CHUNK_SIZE 1200u

struct net_control
{
    net_config config;
    net_buffer buffer;
    FILE *file;
    long file_read_offset;
    long file_write_offset;
    net_socket socket_handle;
    net_socket_address peer_address;
    int has_peer;
    uint16_t bound_port;
    uint32_t random_state;
};

void net_config_init(net_config *config)
{
    if (config == NULL)
        return;

    config->transport = net_TRANSPORT_FILE;
    config->file_path = NULL;
    config->host = "127.0.0.1";
    config->bind_port = 0u;
    config->peer_port = 0u;
    config->buffer_capacity = net_DEFAULT_BUFFER_CAPACITY;
    config->bytes_per_second = 0u;
    config->loss_probability = 0.0;
    config->random_seed = 1u;
}

static dic_status net_validate_config(const net_config *config)
{
    if (config == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (config->buffer_capacity == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (config->loss_probability < 0.0 || config->loss_probability > 1.0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (config->transport == net_TRANSPORT_FILE)
    {
        if (config->file_path == NULL)
            return DIC_STATUS_INVALID_ARGUMENT;
        return DIC_STATUS_OK;
    }
    if (config->transport == net_TRANSPORT_PORT)
        return DIC_STATUS_OK;

    return DIC_STATUS_INVALID_ARGUMENT;
}

static uint32_t net_random_next(net_control *control)
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

static int net_should_drop(net_control *control)
{
    double sample;

    if (control->config.loss_probability <= 0.0)
        return 0;
    if (control->config.loss_probability >= 1.0)
        return 1;

    sample = (double)net_random_next(control) / 4294967295.0;
    return sample < control->config.loss_probability;
}

static void net_throttle(net_control *control, size_t byte_count)
{
    uint64_t milliseconds;

    if (control->config.bytes_per_second == 0u || byte_count == 0u)
        return;

    milliseconds = ((uint64_t)byte_count * 1000u) / control->config.bytes_per_second;
    if ((((uint64_t)byte_count * 1000u) % control->config.bytes_per_second) != 0u)
        ++milliseconds;
    if (milliseconds > 0xffffffffu)
        milliseconds = 0xffffffffu;

    net_platform_sleep_ms((uint32_t)milliseconds);
}

static dic_status net_open_file_transport(net_control *control)
{
    control->file = net_platform_open_file(control->config.file_path, "w+b");
    if (control->file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;

    control->file_read_offset = 0L;
    control->file_write_offset = 0L;
    return DIC_STATUS_OK;
}

static dic_status net_open_port_transport(net_control *control)
{
    dic_status status;

    status = net_socket_startup();
    if (status != DIC_STATUS_OK)
        return status;

    status = net_socket_open_udp(&control->socket_handle, control->config.bind_port);
    if (status != DIC_STATUS_OK)
    {
        net_socket_cleanup();
        return status;
    }

    status = net_socket_bound_port(control->socket_handle, &control->bound_port);
    if (status != DIC_STATUS_OK)
        return status;

    if (control->config.peer_port != 0u)
    {
        status = net_socket_address_ipv4(
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

dic_status net_control_open(net_control **control, const net_config *config)
{
    net_control *opened;
    dic_status status;

    if (control == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    *control = NULL;

    status = net_validate_config(config);
    if (status != DIC_STATUS_OK)
        return status;

    opened = (net_control *)calloc(1u, sizeof(*opened));
    if (opened == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    opened->config = *config;
    opened->socket_handle = net_INVALID_SOCKET;
    opened->random_state = config->random_seed == 0u ? 1u : config->random_seed;
    net_buffer_init(&opened->buffer);

    status = net_buffer_alloc(&opened->buffer, config->buffer_capacity);
    if (status == DIC_STATUS_OK && config->transport == net_TRANSPORT_FILE)
        status = net_open_file_transport(opened);
    if (status == DIC_STATUS_OK && config->transport == net_TRANSPORT_PORT)
        status = net_open_port_transport(opened);

    if (status != DIC_STATUS_OK)
    {
        net_control_close(opened);
        return status;
    }

    *control = opened;
    return DIC_STATUS_OK;
}

void net_control_close(net_control *control)
{
    if (control == NULL)
        return;

    if (control->file != NULL)
        fclose(control->file);
    if (control->socket_handle != net_INVALID_SOCKET)
    {
        net_socket_close(control->socket_handle);
        net_socket_cleanup();
    }

    net_buffer_free(&control->buffer);
    free(control);
}

uint16_t net_control_port(const net_control *control)
{
    if (control == NULL || control->config.transport != net_TRANSPORT_PORT)
        return 0u;

    return control->bound_port;
}

size_t net_control_buffered_bytes(const net_control *control)
{
    if (control == NULL)
        return 0u;

    return control->buffer.length;
}

dic_status net_control_set_rate(net_control *control, uint32_t bytes_per_second)
{
    if (control == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    control->config.bytes_per_second = bytes_per_second;
    return DIC_STATUS_OK;
}

dic_status net_control_set_loss(
    net_control *control,
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

static dic_status net_send_file(net_control *control, const uint8_t *data, size_t size)
{
    if (control->file == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (net_should_drop(control))
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

static dic_status net_send_port(net_control *control, const uint8_t *data, size_t size)
{
    size_t offset = 0u;

    if (!control->has_peer)
        return DIC_STATUS_INVALID_ARGUMENT;

    while (offset < size)
    {
        size_t chunk_size = size - offset;
        dic_status status;

        if (chunk_size > net_PORT_CHUNK_SIZE)
            chunk_size = net_PORT_CHUNK_SIZE;

        if (!net_should_drop(control))
        {
            status = net_socket_send_to(
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

dic_status net_send(
    net_control *control,
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

    net_throttle(control, size);

    if (control->config.transport == net_TRANSPORT_FILE)
        status = net_send_file(control, (const uint8_t *)data, size);
    else
        status = net_send_port(control, (const uint8_t *)data, size);

    if (status == DIC_STATUS_OK && sent != NULL)
        *sent = size;

    return status;
}

static dic_status net_drain_file(net_control *control)
{
    uint8_t temp[1024];

    if (control->file == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    while (net_buffer_free_space(&control->buffer) > 0u)
    {
        size_t read_capacity = net_buffer_free_space(&control->buffer);
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

        if (net_buffer_push(&control->buffer, temp, read_count) != DIC_STATUS_OK)
            return DIC_STATUS_IO_ERROR;
        control->file_read_offset += (long)read_count;
    }

    return DIC_STATUS_OK;
}

static dic_status net_drain_port(net_control *control)
{
    uint8_t temp[net_PORT_CHUNK_SIZE];

    while (net_buffer_free_space(&control->buffer) > 0u)
    {
        size_t received = 0u;
        dic_status status;

        status = net_socket_receive_available(
            control->socket_handle,
            temp,
            sizeof(temp),
            &received
        );
        if (status != DIC_STATUS_OK)
            return status;
        if (received == 0u)
            return DIC_STATUS_OK;
        if (received > net_buffer_free_space(&control->buffer))
            return DIC_STATUS_IO_ERROR;

        status = net_buffer_push(&control->buffer, temp, received);
        if (status != DIC_STATUS_OK)
            return status;
    }

    return DIC_STATUS_OK;
}

dic_status net_receive(
    net_control *control,
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

    if (control->config.transport == net_TRANSPORT_FILE)
        status = net_drain_file(control);
    else
        status = net_drain_port(control);
    if (status != DIC_STATUS_OK)
        return status;

    if (received != NULL)
        *received = net_buffer_pop(&control->buffer, (uint8_t *)data, capacity);
    else
        (void)net_buffer_pop(&control->buffer, (uint8_t *)data, capacity);

    return DIC_STATUS_OK;
}

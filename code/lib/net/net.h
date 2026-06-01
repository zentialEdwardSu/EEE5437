#pragma once

/**
 * net is a small transport shim for tests and coursework tools that need a
 * controllable byte channel.  It can send bytes through a UDP port, a TCP
 * stream, or a file-backed channel, can throttle accepted bytes, can simulate
 * packet loss, and keeps received bytes in a bounded internal buffer.  TCP
 * handles listen when peer_port is zero and connect to host:peer_port when
 * peer_port is nonzero.
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum net_transport
{
    net_TRANSPORT_FILE = 1,
    net_TRANSPORT_PORT = 2,
    net_TRANSPORT_TCP = 3
} net_transport;

typedef struct net_config
{
    net_transport transport;
    const char *file_path;
    const char *host;
    uint16_t bind_port;
    uint16_t peer_port;
    size_t buffer_capacity;
    uint32_t bytes_per_second;
    double loss_probability;
    uint32_t random_seed;
} net_config;

typedef struct net_control net_control;

/**
 * Initializes a transport configuration with safe defaults.
 */
void net_config_init(net_config *config);

/**
 * Opens a net control handle from a validated configuration.
 */
dic_status net_control_open(net_control **control, const net_config *config);

/**
 * Closes a net control handle and releases its socket, file, and buffer.
 */
void net_control_close(net_control *control);

/**
 * Returns the bound socket port, or zero when the handle is not socket-backed.
 */
uint16_t net_control_port(const net_control *control);

/**
 * Returns the number of bytes currently stored in the receive buffer.
 */
size_t net_control_buffered_bytes(const net_control *control);

/**
 * Updates the byte-per-second throttle for future sends; zero disables it.
 */
dic_status net_control_set_rate(net_control *control, uint32_t bytes_per_second);

/**
 * Updates the packet-loss probability and random seed for future sends.
 */
dic_status net_control_set_loss(
    net_control *control,
    double loss_probability,
    uint32_t random_seed
);

/**
 * Sends bytes through the configured transport, applying throttling and packet
 * loss before the transport write.
 */
dic_status net_send(
    net_control *control,
    const void *data,
    size_t size,
    size_t *sent
);

/**
 * Receives bytes from the configured transport into the caller's buffer after
 * first draining available transport data into the internal buffer.
 */
dic_status net_receive(
    net_control *control,
    void *data,
    size_t capacity,
    size_t *received
);

#ifdef __cplusplus
}
#endif

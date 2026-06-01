#pragma once

/**
 * libnet is a small transport shim for tests and coursework tools that need a
 * controllable byte channel.  It can send bytes through either a UDP port or a
 * file-backed channel, can throttle accepted bytes, can simulate packet loss,
 * and keeps received bytes in a bounded internal buffer.
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum libnet_transport
{
    LIBNET_TRANSPORT_FILE = 1,
    LIBNET_TRANSPORT_PORT = 2
} libnet_transport;

typedef struct libnet_config
{
    libnet_transport transport;
    const char *file_path;
    const char *host;
    uint16_t bind_port;
    uint16_t peer_port;
    size_t buffer_capacity;
    uint32_t bytes_per_second;
    double loss_probability;
    uint32_t random_seed;
} libnet_config;

typedef struct libnet_control libnet_control;

/**
 * Initializes a transport configuration with safe defaults.
 */
void libnet_config_init(libnet_config *config);

/**
 * Opens a libnet control handle from a validated configuration.
 */
dic_status libnet_control_open(libnet_control **control, const libnet_config *config);

/**
 * Closes a libnet control handle and releases its socket, file, and buffer.
 */
void libnet_control_close(libnet_control *control);

/**
 * Returns the bound UDP port, or zero when the handle is not port-backed.
 */
uint16_t libnet_control_port(const libnet_control *control);

/**
 * Returns the number of bytes currently stored in the receive buffer.
 */
size_t libnet_control_buffered_bytes(const libnet_control *control);

/**
 * Updates the byte-per-second throttle for future sends; zero disables it.
 */
dic_status libnet_control_set_rate(libnet_control *control, uint32_t bytes_per_second);

/**
 * Updates the packet-loss probability and random seed for future sends.
 */
dic_status libnet_control_set_loss(
    libnet_control *control,
    double loss_probability,
    uint32_t random_seed
);

/**
 * Sends bytes through the configured transport, applying throttling and packet
 * loss before the transport write.
 */
dic_status libnet_send(
    libnet_control *control,
    const void *data,
    size_t size,
    size_t *sent
);

/**
 * Receives bytes from the configured transport into the caller's buffer after
 * first draining available transport data into the internal buffer.
 */
dic_status libnet_receive(
    libnet_control *control,
    void *data,
    size_t capacity,
    size_t *received
);

#ifdef __cplusplus
}
#endif

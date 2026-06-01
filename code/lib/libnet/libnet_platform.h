#pragma once

/**
 * Internal platform layer for libnet sockets, file opening, and millisecond
 * sleeps.  The public library remains C11 while this file hides the Windows and
 * POSIX socket differences.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "errors/errors.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET libnet_socket;
#define LIBNET_INVALID_SOCKET INVALID_SOCKET
#else
typedef int libnet_socket;
#define LIBNET_INVALID_SOCKET (-1)
#endif

typedef struct libnet_socket_address
{
    uint8_t bytes[32];
    size_t size;
} libnet_socket_address;

/**
 * Opens a file with portable MSVC and C library behavior.
 */
FILE *libnet_platform_open_file(const char *path, const char *mode);

/**
 * Sleeps for a whole number of milliseconds.
 */
void libnet_platform_sleep_ms(uint32_t milliseconds);

/**
 * Starts the platform socket subsystem when required.
 */
dic_status libnet_socket_startup(void);

/**
 * Stops the platform socket subsystem when required.
 */
void libnet_socket_cleanup(void);

/**
 * Opens a nonblocking UDP socket bound to the requested local port.
 */
dic_status libnet_socket_open_udp(libnet_socket *socket_handle, uint16_t bind_port);

/**
 * Closes a socket handle.
 */
void libnet_socket_close(libnet_socket socket_handle);

/**
 * Reads the local port currently bound to the socket.
 */
dic_status libnet_socket_bound_port(libnet_socket socket_handle, uint16_t *port);

/**
 * Parses an IPv4 host and port into a socket address.
 */
dic_status libnet_socket_address_ipv4(
    const char *host,
    uint16_t port,
    libnet_socket_address *address
);

/**
 * Sends one datagram to the provided address.
 */
dic_status libnet_socket_send_to(
    libnet_socket socket_handle,
    const libnet_socket_address *address,
    const uint8_t *data,
    size_t size
);

/**
 * Receives one datagram if immediately available.
 */
dic_status libnet_socket_receive_available(
    libnet_socket socket_handle,
    uint8_t *data,
    size_t capacity,
    size_t *received
);

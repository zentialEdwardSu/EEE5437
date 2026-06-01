#pragma once

/**
 * Internal platform layer for net sockets, file opening, and millisecond
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
typedef SOCKET net_socket;
#define net_INVALID_SOCKET INVALID_SOCKET
#else
typedef int net_socket;
#define net_INVALID_SOCKET (-1)
#endif

typedef struct net_socket_address
{
    uint8_t bytes[32];
    size_t size;
} net_socket_address;

/**
 * Opens a file with portable MSVC and C library behavior.
 */
FILE *net_platform_open_file(const char *path, const char *mode);

/**
 * Sleeps for a whole number of milliseconds.
 */
void net_platform_sleep_ms(uint32_t milliseconds);

/**
 * Starts the platform socket subsystem when required.
 */
dic_status net_socket_startup(void);

/**
 * Stops the platform socket subsystem when required.
 */
void net_socket_cleanup(void);

/**
 * Opens a nonblocking UDP socket bound to the requested local port.
 */
dic_status net_socket_open_udp(net_socket *socket_handle, uint16_t bind_port);

/**
 * Opens a nonblocking TCP listener bound to the requested local port.
 */
dic_status net_socket_open_tcp_listener(net_socket *socket_handle, uint16_t bind_port);

/**
 * Opens a nonblocking TCP client connected to the requested peer.
 */
dic_status net_socket_open_tcp_client(
    net_socket *socket_handle,
    const net_socket_address *address,
    uint16_t bind_port
);

/**
 * Closes a socket handle.
 */
void net_socket_close(net_socket socket_handle);

/**
 * Reads the local port currently bound to the socket.
 */
dic_status net_socket_bound_port(net_socket socket_handle, uint16_t *port);

/**
 * Parses an IPv4 host and port into a socket address.
 */
dic_status net_socket_address_ipv4(
    const char *host,
    uint16_t port,
    net_socket_address *address
);

/**
 * Sends one datagram to the provided address.
 */
dic_status net_socket_send_to(
    net_socket socket_handle,
    const net_socket_address *address,
    const uint8_t *data,
    size_t size
);

/**
 * Accepts one TCP client connection if immediately available.
 */
dic_status net_socket_accept_available(
    net_socket listener,
    net_socket *accepted
);

/**
 * Sends bytes through a connected TCP stream.
 */
dic_status net_socket_send_stream(
    net_socket socket_handle,
    const uint8_t *data,
    size_t size
);

/**
 * Receives bytes from a socket if immediately available.
 */
dic_status net_socket_receive_available(
    net_socket socket_handle,
    uint8_t *data,
    size_t capacity,
    size_t *received
);

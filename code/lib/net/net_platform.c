#include "net/net_platform.h"

/**
 * Implements net's portable UDP, TCP, and sleep operations.
 */

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#endif

FILE* net_platform_open_file(const char* path, const char* mode) {
    FILE* file = NULL;

    if (path == NULL || mode == NULL) return NULL;

#if defined(_MSC_VER)
    if (fopen_s(&file, path, mode) != 0) return NULL;
    return file;
#else
    return fopen(path, mode);
#endif
}

void net_platform_sleep_ms(uint32_t milliseconds) {
    if (milliseconds == 0u) return;

#if defined(_WIN32)
    Sleep((DWORD)milliseconds);
#else
    {
        struct timespec duration;

        duration.tv_sec = (time_t)(milliseconds / 1000u);
        duration.tv_nsec = (long)((milliseconds % 1000u) * 1000000u);
        while (nanosleep(&duration, &duration) != 0 && errno == EINTR) {
        }
    }
#endif
}

dic_status net_socket_startup(void) {
#if defined(_WIN32)
    WSADATA data;

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return DIC_STATUS_IO_ERROR;
#endif

    return DIC_STATUS_OK;
}

void net_socket_cleanup(void) {
#if defined(_WIN32)
    WSACleanup();
#endif
}

static dic_status net_socket_make_nonblocking(net_socket socket_handle) {
#if defined(_WIN32)
    u_long mode = 1u;

    if (ioctlsocket(socket_handle, FIONBIO, &mode) != 0)
        return DIC_STATUS_IO_ERROR;
#else
    int flags = fcntl(socket_handle, F_GETFL, 0);

    if (flags < 0) return DIC_STATUS_IO_ERROR;
    if (fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) != 0)
        return DIC_STATUS_IO_ERROR;
#endif

    return DIC_STATUS_OK;
}

static void net_socket_any_ipv4_address(struct sockaddr_in* address,
                                        uint16_t port) {
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(INADDR_ANY);
    address->sin_port = htons(port);
}

static dic_status net_socket_bind_ipv4(net_socket socket_handle,
                                       uint16_t bind_port) {
    struct sockaddr_in address;

    net_socket_any_ipv4_address(&address, bind_port);
    if (bind(socket_handle, (const struct sockaddr*)&address,
             sizeof(address)) != 0)
        return DIC_STATUS_IO_ERROR;

    return DIC_STATUS_OK;
}

static dic_status net_socket_open_ipv4(net_socket* socket_handle,
                                       int socket_type, int protocol,
                                       uint16_t bind_port, int bind_requested,
                                       int nonblocking_requested) {
    net_socket opened;

    if (socket_handle == NULL) return DIC_STATUS_INVALID_ARGUMENT;

    opened = socket(AF_INET, socket_type, protocol);
    if (opened == net_INVALID_SOCKET) return DIC_STATUS_IO_ERROR;

    if (bind_requested &&
        net_socket_bind_ipv4(opened, bind_port) != DIC_STATUS_OK) {
        net_socket_close(opened);
        return DIC_STATUS_IO_ERROR;
    }

    if (nonblocking_requested &&
        net_socket_make_nonblocking(opened) != DIC_STATUS_OK) {
        net_socket_close(opened);
        return DIC_STATUS_IO_ERROR;
    }

    *socket_handle = opened;
    return DIC_STATUS_OK;
}

dic_status net_socket_open_udp(net_socket* socket_handle, uint16_t bind_port) {
    return net_socket_open_ipv4(socket_handle, SOCK_DGRAM, IPPROTO_UDP,
                                bind_port, 1, 1);
}

dic_status net_socket_open_tcp_listener(net_socket* socket_handle,
                                        uint16_t bind_port) {
    net_socket opened;
    dic_status status;

    status = net_socket_open_ipv4(&opened, SOCK_STREAM, IPPROTO_TCP, bind_port,
                                  1, 1);
    if (status != DIC_STATUS_OK) return status;

    if (listen(opened, 1) != 0) {
        net_socket_close(opened);
        return DIC_STATUS_IO_ERROR;
    }

    *socket_handle = opened;
    return DIC_STATUS_OK;
}

dic_status net_socket_open_tcp_client(net_socket* socket_handle,
                                      const net_socket_address* address,
                                      uint16_t bind_port) {
    net_socket opened;
    dic_status status;

    if (socket_handle == NULL || address == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = net_socket_open_ipv4(&opened, SOCK_STREAM, IPPROTO_TCP, bind_port,
                                  bind_port != 0u, 0);
    if (status != DIC_STATUS_OK) return status;

    if (connect(opened, (const struct sockaddr*)address->bytes,
#if defined(_WIN32)
                (int)address->size
#else
                (socklen_t)address->size
#endif
                ) != 0) {
        net_socket_close(opened);
        return DIC_STATUS_IO_ERROR;
    }

    if (net_socket_make_nonblocking(opened) != DIC_STATUS_OK) {
        net_socket_close(opened);
        return DIC_STATUS_IO_ERROR;
    }

    *socket_handle = opened;
    return DIC_STATUS_OK;
}

void net_socket_close(net_socket socket_handle) {
    if (socket_handle == net_INVALID_SOCKET) return;

#if defined(_WIN32)
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

dic_status net_socket_bound_port(net_socket socket_handle, uint16_t* port) {
    struct sockaddr_in address;
#if defined(_WIN32)
    int length = (int)sizeof(address);
#else
    socklen_t length = (socklen_t)sizeof(address);
#endif

    if (port == NULL || socket_handle == net_INVALID_SOCKET)
        return DIC_STATUS_INVALID_ARGUMENT;

    memset(&address, 0, sizeof(address));
    if (getsockname(socket_handle, (struct sockaddr*)&address, &length) != 0)
        return DIC_STATUS_IO_ERROR;

    *port = ntohs(address.sin_port);
    return DIC_STATUS_OK;
}

dic_status net_socket_address_ipv4(const char* host, uint16_t port,
                                   net_socket_address* address) {
    struct sockaddr_in ipv4;

    if (address == NULL || port == 0u) return DIC_STATUS_INVALID_ARGUMENT;
    if (host == NULL) host = "127.0.0.1";

    memset(&ipv4, 0, sizeof(ipv4));
    ipv4.sin_family = AF_INET;
    ipv4.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &ipv4.sin_addr) != 1)
        return DIC_STATUS_INVALID_ARGUMENT;

    memset(address, 0, sizeof(*address));
    memcpy(address->bytes, &ipv4, sizeof(ipv4));
    address->size = sizeof(ipv4);
    return DIC_STATUS_OK;
}

dic_status net_socket_send_to(net_socket socket_handle,
                              const net_socket_address* address,
                              const uint8_t* data, size_t size) {
    int result;

    if (socket_handle == net_INVALID_SOCKET || address == NULL ||
        data == NULL || size == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (size > 65507u) return DIC_STATUS_INVALID_ARGUMENT;

    result = sendto(socket_handle, (const char*)data, (int)size, 0,
                    (const struct sockaddr*)address->bytes,
#if defined(_WIN32)
                    (int)address->size
#else
                    (socklen_t)address->size
#endif
    );

    if (result != (int)size) return DIC_STATUS_IO_ERROR;

    return DIC_STATUS_OK;
}

dic_status net_socket_accept_available(net_socket listener,
                                       net_socket* accepted) {
    net_socket opened;

    if (accepted == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    *accepted = net_INVALID_SOCKET;

    if (listener == net_INVALID_SOCKET) return DIC_STATUS_INVALID_ARGUMENT;

    opened = accept(listener, NULL, NULL);
    if (opened != net_INVALID_SOCKET) {
        if (net_socket_make_nonblocking(opened) != DIC_STATUS_OK) {
            net_socket_close(opened);
            return DIC_STATUS_IO_ERROR;
        }
        *accepted = opened;
        return DIC_STATUS_OK;
    }

#if defined(_WIN32)
    {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) return DIC_STATUS_OK;
    }
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) return DIC_STATUS_OK;
#endif

    return DIC_STATUS_IO_ERROR;
}

dic_status net_socket_send_stream(net_socket socket_handle, const uint8_t* data,
                                  size_t size) {
    size_t offset = 0u;
    int wait_attempts = 0;
    const int max_wait_attempts = 100000;

    if (socket_handle == net_INVALID_SOCKET || data == NULL || size == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    while (offset < size) {
        int result = send(socket_handle, (const char*)(data + offset),
                          (int)(size - offset), 0);

        if (result < 0) {
#if defined(_WIN32)
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                if (++wait_attempts >= max_wait_attempts)
                    return DIC_STATUS_IO_ERROR;
                net_platform_sleep_ms(1u);
                continue;
            }
#else
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (++wait_attempts >= max_wait_attempts)
                    return DIC_STATUS_IO_ERROR;
                net_platform_sleep_ms(1u);
                continue;
            }
#endif
            return DIC_STATUS_IO_ERROR;
        }
        if (result == 0) return DIC_STATUS_IO_ERROR;

        offset += (size_t)result;
        wait_attempts = 0;
    }

    return DIC_STATUS_OK;
}

dic_status net_socket_receive_available(net_socket socket_handle, uint8_t* data,
                                        size_t capacity, size_t* received) {
    int result;

    if (received == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    *received = 0u;

    if (socket_handle == net_INVALID_SOCKET || data == NULL || capacity == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (capacity > 65507u) capacity = 65507u;

    result = recv(socket_handle, (char*)data, (int)capacity, 0);
    if (result > 0) {
        *received = (size_t)result;
        return DIC_STATUS_OK;
    }
    if (result == 0) return DIC_STATUS_OK;

#if defined(_WIN32)
    {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) return DIC_STATUS_OK;
    }
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) return DIC_STATUS_OK;
#endif

    return DIC_STATUS_IO_ERROR;
}

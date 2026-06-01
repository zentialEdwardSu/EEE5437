#include "libnet/libnet_platform.h"

/**
 * Implements libnet's portable UDP socket and sleep operations.
 */

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

FILE *libnet_platform_open_file(const char *path, const char *mode)
{
    FILE *file = NULL;

    if (path == NULL || mode == NULL)
        return NULL;

#if defined(_MSC_VER)
    if (fopen_s(&file, path, mode) != 0)
        return NULL;
    return file;
#else
    return fopen(path, mode);
#endif
}

void libnet_platform_sleep_ms(uint32_t milliseconds)
{
    if (milliseconds == 0u)
        return;

#if defined(_WIN32)
    Sleep((DWORD)milliseconds);
#else
    {
        struct timespec duration;

        duration.tv_sec = (time_t)(milliseconds / 1000u);
        duration.tv_nsec = (long)((milliseconds % 1000u) * 1000000u);
        while (nanosleep(&duration, &duration) != 0 && errno == EINTR)
        {
        }
    }
#endif
}

dic_status libnet_socket_startup(void)
{
#if defined(_WIN32)
    WSADATA data;

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return DIC_STATUS_IO_ERROR;
#endif

    return DIC_STATUS_OK;
}

void libnet_socket_cleanup(void)
{
#if defined(_WIN32)
    WSACleanup();
#endif
}

static dic_status libnet_socket_make_nonblocking(libnet_socket socket_handle)
{
#if defined(_WIN32)
    u_long mode = 1u;

    if (ioctlsocket(socket_handle, FIONBIO, &mode) != 0)
        return DIC_STATUS_IO_ERROR;
#else
    int flags = fcntl(socket_handle, F_GETFL, 0);

    if (flags < 0)
        return DIC_STATUS_IO_ERROR;
    if (fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) != 0)
        return DIC_STATUS_IO_ERROR;
#endif

    return DIC_STATUS_OK;
}

dic_status libnet_socket_open_udp(libnet_socket *socket_handle, uint16_t bind_port)
{
    struct sockaddr_in address;
    libnet_socket opened;

    if (socket_handle == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    opened = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (opened == LIBNET_INVALID_SOCKET)
        return DIC_STATUS_IO_ERROR;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(bind_port);

    if (bind(opened, (const struct sockaddr *)&address, sizeof(address)) != 0)
    {
        libnet_socket_close(opened);
        return DIC_STATUS_IO_ERROR;
    }

    if (libnet_socket_make_nonblocking(opened) != DIC_STATUS_OK)
    {
        libnet_socket_close(opened);
        return DIC_STATUS_IO_ERROR;
    }

    *socket_handle = opened;
    return DIC_STATUS_OK;
}

void libnet_socket_close(libnet_socket socket_handle)
{
    if (socket_handle == LIBNET_INVALID_SOCKET)
        return;

#if defined(_WIN32)
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

dic_status libnet_socket_bound_port(libnet_socket socket_handle, uint16_t *port)
{
    struct sockaddr_in address;
#if defined(_WIN32)
    int length = (int)sizeof(address);
#else
    socklen_t length = (socklen_t)sizeof(address);
#endif

    if (port == NULL || socket_handle == LIBNET_INVALID_SOCKET)
        return DIC_STATUS_INVALID_ARGUMENT;

    memset(&address, 0, sizeof(address));
    if (getsockname(socket_handle, (struct sockaddr *)&address, &length) != 0)
        return DIC_STATUS_IO_ERROR;

    *port = ntohs(address.sin_port);
    return DIC_STATUS_OK;
}

dic_status libnet_socket_address_ipv4(
    const char *host,
    uint16_t port,
    libnet_socket_address *address
)
{
    struct sockaddr_in ipv4;

    if (address == NULL || port == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (host == NULL)
        host = "127.0.0.1";

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

dic_status libnet_socket_send_to(
    libnet_socket socket_handle,
    const libnet_socket_address *address,
    const uint8_t *data,
    size_t size
)
{
    int result;

    if (socket_handle == LIBNET_INVALID_SOCKET || address == NULL || data == NULL || size == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (size > 65507u)
        return DIC_STATUS_INVALID_ARGUMENT;

    result = sendto(
        socket_handle,
        (const char *)data,
        (int)size,
        0,
        (const struct sockaddr *)address->bytes,
#if defined(_WIN32)
        (int)address->size
#else
        (socklen_t)address->size
#endif
    );

    if (result != (int)size)
        return DIC_STATUS_IO_ERROR;

    return DIC_STATUS_OK;
}

dic_status libnet_socket_receive_available(
    libnet_socket socket_handle,
    uint8_t *data,
    size_t capacity,
    size_t *received
)
{
    int result;

    if (received == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    *received = 0u;

    if (socket_handle == LIBNET_INVALID_SOCKET || data == NULL || capacity == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (capacity > 65507u)
        capacity = 65507u;

    result = recvfrom(socket_handle, (char *)data, (int)capacity, 0, NULL, NULL);
    if (result > 0)
    {
        *received = (size_t)result;
        return DIC_STATUS_OK;
    }
    if (result == 0)
        return DIC_STATUS_OK;

#if defined(_WIN32)
    {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK)
            return DIC_STATUS_OK;
    }
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return DIC_STATUS_OK;
#endif

    return DIC_STATUS_IO_ERROR;
}

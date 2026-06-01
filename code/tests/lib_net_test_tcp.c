#include <stdint.h>
#include <string.h>

/**
 * Tests net's TCP transport with a loopback listener and client stream.
 */

#include "net/net.h"
#include "net/net_platform.h"
#include "test_helpers.h"

/**
 * Gives the nonblocking TCP listener time to accept and drain local bytes.
 */
static void net_test_pause(void)
{
    net_platform_sleep_ms(1u);
}

/**
 * Verifies that TCP sends ordered stream bytes from a client to a listener.
 */
static void net_test_tcp_loopback(void)
{
    const char first[] = "tcp ";
    const char second[] = "stream bytes";
    const char expected[] = "tcp stream bytes";
    net_config listener_config;
    net_config client_config;
    net_control *listener = NULL;
    net_control *client = NULL;
    char output[32];
    size_t sent = 0u;
    size_t total_received = 0u;
    uint16_t listener_port;
    int attempt;

    net_config_init(&listener_config);
    listener_config.transport = net_TRANSPORT_TCP;
    listener_config.buffer_capacity = 64u;
    DIC_EXPECT(net_control_open(&listener, &listener_config) == DIC_STATUS_OK);

    listener_port = net_control_port(listener);
    DIC_EXPECT(listener_port != 0u);

    net_config_init(&client_config);
    client_config.transport = net_TRANSPORT_TCP;
    client_config.peer_port = listener_port;
    client_config.buffer_capacity = 64u;
    DIC_EXPECT(net_control_open(&client, &client_config) == DIC_STATUS_OK);

    DIC_EXPECT(net_send(client, first, strlen(first), &sent) == DIC_STATUS_OK);
    DIC_EXPECT(sent == strlen(first));
    DIC_EXPECT(net_send(client, second, strlen(second), &sent) == DIC_STATUS_OK);
    DIC_EXPECT(sent == strlen(second));

    memset(output, 0, sizeof(output));
    for (attempt = 0; attempt < 1000 && total_received < strlen(expected); ++attempt)
    {
        size_t received = 0u;

        DIC_EXPECT(net_receive(
            listener,
            output + total_received,
            sizeof(output) - total_received,
            &received
        ) == DIC_STATUS_OK);
        total_received += received;
        if (received == 0u)
            net_test_pause();
    }

    DIC_EXPECT(total_received == strlen(expected));
    DIC_EXPECT(memcmp(output, expected, total_received) == 0);

    net_control_close(client);
    net_control_close(listener);
}

int main(void)
{
    net_test_tcp_loopback();
    return 0;
}

#include <stdint.h>
#include <string.h>

#include "net/net.h"
#include "net/net_platform.h"
#include "test_helpers.h"

static void net_test_pause(void)
{
    net_platform_sleep_ms(1u);
}

static void net_test_port_loopback(void)
{
    const char payload[] = "udp bytes";
    net_config receiver_config;
    net_config sender_config;
    net_control *receiver = NULL;
    net_control *sender = NULL;
    char output[32];
    size_t sent = 0u;
    size_t received = 0u;
    uint16_t receiver_port;
    int attempt;

    net_config_init(&receiver_config);
    receiver_config.transport = net_TRANSPORT_PORT;
    receiver_config.buffer_capacity = 64u;
    DIC_EXPECT(net_control_open(&receiver, &receiver_config) == DIC_STATUS_OK);

    receiver_port = net_control_port(receiver);
    DIC_EXPECT(receiver_port != 0u);

    net_config_init(&sender_config);
    sender_config.transport = net_TRANSPORT_PORT;
    sender_config.peer_port = receiver_port;
    sender_config.buffer_capacity = 64u;
    DIC_EXPECT(net_control_open(&sender, &sender_config) == DIC_STATUS_OK);

    DIC_EXPECT(net_send(sender, payload, strlen(payload), &sent) == DIC_STATUS_OK);
    DIC_EXPECT(sent == strlen(payload));

    memset(output, 0, sizeof(output));
    for (attempt = 0; attempt < 1000 && received == 0u; ++attempt)
    {
        DIC_EXPECT(net_receive(receiver, output, sizeof(output), &received) == DIC_STATUS_OK);
        if (received == 0u)
            net_test_pause();
    }

    DIC_EXPECT(received == strlen(payload));
    DIC_EXPECT(memcmp(output, payload, received) == 0);

    net_control_close(sender);
    net_control_close(receiver);
}

int main(void)
{
    net_test_port_loopback();
    return 0;
}

#include <stdint.h>
#include <string.h>

#include "libnet/libnet.h"
#include "libnet/libnet_platform.h"
#include "test_helpers.h"

static void libnet_test_pause(void)
{
    libnet_platform_sleep_ms(1u);
}

static void libnet_test_port_loopback(void)
{
    const char payload[] = "udp bytes";
    libnet_config receiver_config;
    libnet_config sender_config;
    libnet_control *receiver = NULL;
    libnet_control *sender = NULL;
    char output[32];
    size_t sent = 0u;
    size_t received = 0u;
    uint16_t receiver_port;
    int attempt;

    libnet_config_init(&receiver_config);
    receiver_config.transport = LIBNET_TRANSPORT_PORT;
    receiver_config.buffer_capacity = 64u;
    DIC_EXPECT(libnet_control_open(&receiver, &receiver_config) == DIC_STATUS_OK);

    receiver_port = libnet_control_port(receiver);
    DIC_EXPECT(receiver_port != 0u);

    libnet_config_init(&sender_config);
    sender_config.transport = LIBNET_TRANSPORT_PORT;
    sender_config.peer_port = receiver_port;
    sender_config.buffer_capacity = 64u;
    DIC_EXPECT(libnet_control_open(&sender, &sender_config) == DIC_STATUS_OK);

    DIC_EXPECT(libnet_send(sender, payload, strlen(payload), &sent) == DIC_STATUS_OK);
    DIC_EXPECT(sent == strlen(payload));

    memset(output, 0, sizeof(output));
    for (attempt = 0; attempt < 1000 && received == 0u; ++attempt)
    {
        DIC_EXPECT(libnet_receive(receiver, output, sizeof(output), &received) == DIC_STATUS_OK);
        if (received == 0u)
            libnet_test_pause();
    }

    DIC_EXPECT(received == strlen(payload));
    DIC_EXPECT(memcmp(output, payload, received) == 0);

    libnet_control_close(sender);
    libnet_control_close(receiver);
}

int main(void)
{
    libnet_test_port_loopback();
    return 0;
}

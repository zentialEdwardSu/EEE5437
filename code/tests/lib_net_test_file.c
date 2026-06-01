#include <stdio.h>
#include <string.h>
#include <time.h>

#include "net/net.h"
#include "test_helpers.h"

static void net_test_file_buffering(void)
{
    const char *path = "net_file_transport_test.bin";
    const char payload[] = "abcdef";
    net_config config;
    net_control *control = NULL;
    char output[8];
    size_t count = 0u;

    remove(path);
    net_config_init(&config);
    config.transport = net_TRANSPORT_FILE;
    config.file_path = path;
    config.buffer_capacity = 16u;

    DIC_EXPECT(net_control_open(&control, &config) == DIC_STATUS_OK);
    DIC_EXPECT(net_send(control, payload, 6u, &count) == DIC_STATUS_OK);
    DIC_EXPECT(count == 6u);

    memset(output, 0, sizeof(output));
    DIC_EXPECT(net_receive(control, output, 2u, &count) == DIC_STATUS_OK);
    DIC_EXPECT(count == 2u);
    DIC_EXPECT(memcmp(output, "ab", 2u) == 0);
    DIC_EXPECT(net_control_buffered_bytes(control) == 4u);

    memset(output, 0, sizeof(output));
    DIC_EXPECT(net_receive(control, output, sizeof(output), &count) == DIC_STATUS_OK);
    DIC_EXPECT(count == 4u);
    DIC_EXPECT(memcmp(output, "cdef", 4u) == 0);
    DIC_EXPECT(net_control_buffered_bytes(control) == 0u);

    net_control_close(control);
    remove(path);
}

static void net_test_file_loss(void)
{
    const char *path = "net_file_loss_test.bin";
    const char payload[] = "lost";
    net_config config;
    net_control *control = NULL;
    char output[8];
    size_t count = 0u;

    remove(path);
    net_config_init(&config);
    config.transport = net_TRANSPORT_FILE;
    config.file_path = path;
    config.buffer_capacity = 16u;

    DIC_EXPECT(net_control_open(&control, &config) == DIC_STATUS_OK);
    DIC_EXPECT(net_control_set_loss(control, 1.0, 7u) == DIC_STATUS_OK);
    DIC_EXPECT(net_send(control, payload, 4u, &count) == DIC_STATUS_OK);
    DIC_EXPECT(count == 4u);
    DIC_EXPECT(net_receive(control, output, sizeof(output), &count) == DIC_STATUS_OK);
    DIC_EXPECT(count == 0u);

    net_control_close(control);
    remove(path);
}

static void net_test_file_throttle(void)
{
    const char *path = "net_file_throttle_test.bin";
    const char payload[] = "x";
    net_config config;
    net_control *control = NULL;
    time_t before;
    time_t after;
    size_t count = 0u;

    remove(path);
    net_config_init(&config);
    config.transport = net_TRANSPORT_FILE;
    config.file_path = path;
    config.buffer_capacity = 16u;

    DIC_EXPECT(net_control_open(&control, &config) == DIC_STATUS_OK);
    DIC_EXPECT(net_control_set_rate(control, 1u) == DIC_STATUS_OK);
    before = time(NULL);
    DIC_EXPECT(net_send(control, payload, 1u, &count) == DIC_STATUS_OK);
    after = time(NULL);
    DIC_EXPECT(count == 1u);
    DIC_EXPECT(difftime(after, before) >= 1.0);

    net_control_close(control);
    remove(path);
}

int main(void)
{
    net_test_file_buffering();
    net_test_file_loss();
    net_test_file_throttle();
    return 0;
}

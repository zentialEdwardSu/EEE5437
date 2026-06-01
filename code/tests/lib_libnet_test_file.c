#include <stdio.h>
#include <string.h>
#include <time.h>

#include "libnet/libnet.h"
#include "test_helpers.h"

static void libnet_test_file_buffering(void)
{
    const char *path = "libnet_file_transport_test.bin";
    const char payload[] = "abcdef";
    libnet_config config;
    libnet_control *control = NULL;
    char output[8];
    size_t count = 0u;

    remove(path);
    libnet_config_init(&config);
    config.transport = LIBNET_TRANSPORT_FILE;
    config.file_path = path;
    config.buffer_capacity = 16u;

    DIC_EXPECT(libnet_control_open(&control, &config) == DIC_STATUS_OK);
    DIC_EXPECT(libnet_send(control, payload, 6u, &count) == DIC_STATUS_OK);
    DIC_EXPECT(count == 6u);

    memset(output, 0, sizeof(output));
    DIC_EXPECT(libnet_receive(control, output, 2u, &count) == DIC_STATUS_OK);
    DIC_EXPECT(count == 2u);
    DIC_EXPECT(memcmp(output, "ab", 2u) == 0);
    DIC_EXPECT(libnet_control_buffered_bytes(control) == 4u);

    memset(output, 0, sizeof(output));
    DIC_EXPECT(libnet_receive(control, output, sizeof(output), &count) == DIC_STATUS_OK);
    DIC_EXPECT(count == 4u);
    DIC_EXPECT(memcmp(output, "cdef", 4u) == 0);
    DIC_EXPECT(libnet_control_buffered_bytes(control) == 0u);

    libnet_control_close(control);
    remove(path);
}

static void libnet_test_file_loss(void)
{
    const char *path = "libnet_file_loss_test.bin";
    const char payload[] = "lost";
    libnet_config config;
    libnet_control *control = NULL;
    char output[8];
    size_t count = 0u;

    remove(path);
    libnet_config_init(&config);
    config.transport = LIBNET_TRANSPORT_FILE;
    config.file_path = path;
    config.buffer_capacity = 16u;

    DIC_EXPECT(libnet_control_open(&control, &config) == DIC_STATUS_OK);
    DIC_EXPECT(libnet_control_set_loss(control, 1.0, 7u) == DIC_STATUS_OK);
    DIC_EXPECT(libnet_send(control, payload, 4u, &count) == DIC_STATUS_OK);
    DIC_EXPECT(count == 4u);
    DIC_EXPECT(libnet_receive(control, output, sizeof(output), &count) == DIC_STATUS_OK);
    DIC_EXPECT(count == 0u);

    libnet_control_close(control);
    remove(path);
}

static void libnet_test_file_throttle(void)
{
    const char *path = "libnet_file_throttle_test.bin";
    const char payload[] = "x";
    libnet_config config;
    libnet_control *control = NULL;
    time_t before;
    time_t after;
    size_t count = 0u;

    remove(path);
    libnet_config_init(&config);
    config.transport = LIBNET_TRANSPORT_FILE;
    config.file_path = path;
    config.buffer_capacity = 16u;

    DIC_EXPECT(libnet_control_open(&control, &config) == DIC_STATUS_OK);
    DIC_EXPECT(libnet_control_set_rate(control, 1u) == DIC_STATUS_OK);
    before = time(NULL);
    DIC_EXPECT(libnet_send(control, payload, 1u, &count) == DIC_STATUS_OK);
    after = time(NULL);
    DIC_EXPECT(count == 1u);
    DIC_EXPECT(difftime(after, before) >= 1.0);

    libnet_control_close(control);
    remove(path);
}

int main(void)
{
    libnet_test_file_buffering();
    libnet_test_file_loss();
    libnet_test_file_throttle();
    return 0;
}

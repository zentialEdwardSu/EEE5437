#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * Tests the full network send/receive pipeline on TCP loopback:
 * encode -> serialize -> TCP send -> TCP receive -> deserialize -> decode.
 */

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "codec/metrics.h"
#include "net/net.h"
#include "net/net_platform.h"
#include "test_helpers.h"

static void net_test_pause(void) { net_platform_sleep_ms(1u); }

static void net_test_codec_loopback(void) {
    uint8_t source[32 * 32];
    codec_basic_encoded_image encoded = {0};
    uint8_t* ser_buf = NULL;
    size_t ser_size = 0u;
    net_config listener_cfg;
    net_config client_cfg;
    net_control* listener = NULL;
    net_control* client = NULL;
    uint16_t listener_port;
    uint8_t header[4];
    uint32_t payload_size;
    uint8_t* recv_buf = NULL;
    size_t total_received;
    int attempt;
    int x, y;
    size_t sent;

    /* Build test image */
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            source[(size_t)y * 32u + (size_t)x] =
                (uint8_t)((x * 7 + y * 13) & 0xff);

    /* Encode and serialize */
    DIC_EXPECT(codec_basic_encode_image(source, 32, 32, 1, 3, 2.5f, 0, &encoded) ==
               DIC_STATUS_OK);
    DIC_EXPECT(codec_basic_serialize(&encoded, &ser_buf, &ser_size) ==
               DIC_STATUS_OK);
    DIC_EXPECT(ser_buf != NULL);
    DIC_EXPECT(ser_size > 0u);

    /* Start listener */
    net_config_init(&listener_cfg);
    listener_cfg.transport = net_TRANSPORT_TCP;
    listener_cfg.buffer_capacity = 256u * 1024u;
    DIC_EXPECT(net_control_open(&listener, &listener_cfg) == DIC_STATUS_OK);
    listener_port = net_control_port(listener);
    DIC_EXPECT(listener_port != 0u);

    /* Connect client */
    net_config_init(&client_cfg);
    client_cfg.transport = net_TRANSPORT_TCP;
    client_cfg.peer_port = listener_port;
    client_cfg.buffer_capacity = 256u * 1024u;
    DIC_EXPECT(net_control_open(&client, &client_cfg) == DIC_STATUS_OK);

    /* Send header (4-byte payload size LE) */
    header[0] = (uint8_t)(ser_size & 0xffu);
    header[1] = (uint8_t)((ser_size >> 8) & 0xffu);
    header[2] = (uint8_t)((ser_size >> 16) & 0xffu);
    header[3] = (uint8_t)((ser_size >> 24) & 0xffu);
    DIC_EXPECT(net_send(client, header, sizeof(header), &sent) ==
               DIC_STATUS_OK);
    DIC_EXPECT(sent == sizeof(header));

    /* Send payload */
    DIC_EXPECT(net_send(client, ser_buf, ser_size, &sent) == DIC_STATUS_OK);
    DIC_EXPECT(sent == ser_size);

    /* Receive header on listener side */
    {
        uint8_t hdr[4];
        size_t received_hdr = 0u;
        for (attempt = 0; attempt < 5000 && received_hdr < 4u; ++attempt) {
            size_t got = 0u;
            DIC_EXPECT(net_receive(listener, hdr + received_hdr,
                                   4u - received_hdr, &got) == DIC_STATUS_OK);
            received_hdr += got;
            if (got == 0u) net_test_pause();
        }
        DIC_EXPECT(received_hdr == 4u);
        payload_size = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                       ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    }
    DIC_EXPECT(payload_size == (uint32_t)ser_size);

    /* Receive payload */
    recv_buf = (uint8_t*)malloc(payload_size);
    DIC_EXPECT(recv_buf != NULL);
    total_received = 0u;
    for (attempt = 0; attempt < 5000 && total_received < payload_size;
         ++attempt) {
        size_t got = 0u;
        DIC_EXPECT(net_receive(listener, recv_buf + total_received,
                               payload_size - total_received,
                               &got) == DIC_STATUS_OK);
        total_received += got;
        if (got == 0u) net_test_pause();
    }
    DIC_EXPECT(total_received == payload_size);

    /* Deserialize and verify */
    {
        codec_basic_encoded_image decoded_enc = {0};
        dic_image_u8 decoded_img = {0};
        double psnr;

        DIC_EXPECT(codec_basic_deserialize(recv_buf, payload_size,
                                           &decoded_enc) == DIC_STATUS_OK);
        DIC_EXPECT(decoded_enc.width == 32);
        DIC_EXPECT(decoded_enc.height == 32);
        DIC_EXPECT(decoded_enc.channels == 1);
        DIC_EXPECT(decoded_enc.levels == 3);
        DIC_EXPECT(decoded_enc.quant_step == 2.5f);

        /* Decode and check PSNR */
        DIC_EXPECT(codec_basic_decode_image(&decoded_enc, 3, 0, &decoded_img) ==
                   DIC_STATUS_OK);
        psnr = codec_metric_psnr_u8(source, decoded_img.data, 32u * 32u);
        DIC_EXPECT(psnr > 25.0);

        dic_image_u8_free(&decoded_img);
        codec_basic_encoded_free(&decoded_enc);
    }

    /* Cleanup */
    free(recv_buf);
    free(ser_buf);
    net_control_close(client);
    net_control_close(listener);
    codec_basic_encoded_free(&encoded);
}

int main(void) {
    net_test_codec_loopback();
    return 0;
}

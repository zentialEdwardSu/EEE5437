#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bits/bits.h"
#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "codec/metrics.h"
#include "test_helpers.h"

static FILE* open_test_stream(void) {
#if defined(_WIN32)
    FILE* stream = NULL;
    if (tmpfile_s(&stream) != 0) return NULL;
    return stream;
#else
    return tmpfile();
#endif
}

int main(void) {
    uint8_t source[32 * 32];
    codec_basic_encoded_image encoded = {0};
    uint8_t* buffer = NULL;
    size_t buffer_size = 0u;
    int y, x;

    /* Build test image */
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            source[(size_t)y * 32u + (size_t)x] =
                (uint8_t)(20 + x * 2 + y * 3 + ((x + y) % 7));

    /* Encode */
    DIC_EXPECT(codec_basic_encode_image(source, 32, 32, 1, 3, 2.5f, &encoded) ==
               DIC_STATUS_OK);

    /* --- Test 1: Serialize roundtrip --- */
    DIC_EXPECT(codec_basic_serialize(&encoded, &buffer, &buffer_size) ==
               DIC_STATUS_OK);
    DIC_EXPECT(buffer != NULL);
    DIC_EXPECT(buffer_size > 32u);

    /* Deserialize */
    {
        codec_basic_encoded_image deserialized = {0};
        dic_image_u8 decoded = {0};
        double psnr;

        DIC_EXPECT(codec_basic_deserialize(buffer, buffer_size,
                                           &deserialized) == DIC_STATUS_OK);
        DIC_EXPECT(deserialized.width == 32);
        DIC_EXPECT(deserialized.height == 32);
        DIC_EXPECT(deserialized.channels == 1);
        DIC_EXPECT(deserialized.levels == 3);
        DIC_EXPECT(deserialized.quant_step == 2.5f);

        /* Full decode from deserialized data */
        DIC_EXPECT(codec_basic_decode_image(&deserialized, 0, &decoded) ==
                   DIC_STATUS_OK);
        DIC_EXPECT(decoded.width == 32);
        DIC_EXPECT(decoded.height == 32);
        psnr = codec_metric_psnr_u8(source, decoded.data, 32u * 32u);
        DIC_EXPECT(psnr > 25.0);

        dic_image_u8_free(&decoded);
        codec_basic_encoded_free(&deserialized);
    }

    free(buffer);
    buffer = NULL;

    /* --- Test 2: RGB payload is serialized layer-major --- */
    {
        uint8_t rgb_source[16 * 16 * 3];
        codec_basic_encoded_image rgb_encoded = {0};
        uint8_t* rgb_buffer = NULL;
        size_t rgb_size = 0u;
        size_t offset;
        int declared_maximum;
        int maximum = 0;
        int pixel, channel, layer;

        for (pixel = 0; pixel < 16 * 16; ++pixel) {
            rgb_source[(size_t)pixel * 3u] = (uint8_t)(pixel % 251);
            rgb_source[(size_t)pixel * 3u + 1u] =
                (uint8_t)((pixel * 3 + 17) % 251);
            rgb_source[(size_t)pixel * 3u + 2u] =
                (uint8_t)((pixel * 7 + 29) % 251);
        }
        DIC_EXPECT(codec_basic_encode_image(rgb_source, 16, 16, 3, 2, 3.25f,
                                            &rgb_encoded) ==
                   DIC_STATUS_OK);
        DIC_EXPECT(codec_basic_serialize(&rgb_encoded, &rgb_buffer,
                                         &rgb_size) == DIC_STATUS_OK);
        DIC_EXPECT(memcmp(rgb_buffer, DIC_BASIC_FILE_MAGIC, 4u) == 0);
        DIC_EXPECT(bits_u32_from_le(rgb_buffer + 4u) ==
                   DIC_BASIC_FILE_VERSION);
        DIC_EXPECT(bits_float_from_bits(bits_u32_from_le(rgb_buffer + 24u)) ==
                   3.25f);

        offset = 4u + 6u * 4u + DIC_SCAN_TOKEN_COUNT;
        declared_maximum = (int)bits_u32_from_le(rgb_buffer + offset);
        offset += 4u;
        for (channel = 0; channel < rgb_encoded.channels; ++channel) {
            DIC_EXPECT(bits_u32_from_le(rgb_buffer + offset) ==
                       (uint32_t)rgb_encoded
                           .channel_streams[channel]
                           .num_bitplanes);
            if (rgb_encoded.channel_streams[channel].num_bitplanes > maximum)
                maximum =
                    rgb_encoded.channel_streams[channel].num_bitplanes;
            offset += 4u;
        }
        DIC_EXPECT(declared_maximum == maximum);
        for (layer = 0; layer < maximum; ++layer) {
            for (channel = 0; channel < rgb_encoded.channels; ++channel) {
                const codec_basic_channel_stream* stream =
                    rgb_encoded.channel_streams + channel;
                if (layer >= stream->num_bitplanes) continue;
                offset += codec_basic_bitplane_byte_size(
                    stream->bitplanes + layer);
                DIC_EXPECT(offset <= rgb_size);
            }
            DIC_EXPECT(bits_u32_from_le(rgb_buffer + offset) ==
                       DIC_BASIC_LAYER_END_MARKER);
            offset += 4u;
        }
        DIC_EXPECT(offset == rgb_size);

        free(rgb_buffer);
        codec_basic_encoded_free(&rgb_encoded);
    }

    /* --- Test 3: Direct stream output is byte-identical --- */
    {
        FILE* stream = open_test_stream();
        uint8_t* stream_bytes = NULL;
        long stream_length;

        DIC_EXPECT(stream != NULL);
        DIC_EXPECT(codec_basic_write_stream(stream, &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(fflush(stream) == 0);
        DIC_EXPECT(fseek(stream, 0, SEEK_END) == 0);
        stream_length = ftell(stream);
        DIC_EXPECT(stream_length > 0);
        DIC_EXPECT((size_t)stream_length == buffer_size);
        DIC_EXPECT(fseek(stream, 0, SEEK_SET) == 0);

        stream_bytes = (uint8_t*)malloc((size_t)stream_length);
        DIC_EXPECT(stream_bytes != NULL);
        DIC_EXPECT(fread(stream_bytes, 1u, (size_t)stream_length, stream) ==
                   (size_t)stream_length);

        DIC_EXPECT(codec_basic_serialize(&encoded, &buffer, &buffer_size) ==
                   DIC_STATUS_OK);
        DIC_EXPECT(memcmp(stream_bytes, buffer, buffer_size) == 0);

        {
            codec_basic_encoded_image streamed = {0};
            dic_image_u8 streamed_image = {0};
            DIC_EXPECT(fseek(stream, 0, SEEK_SET) == 0);
            DIC_EXPECT(codec_basic_read_stream(stream, &streamed) ==
                       DIC_STATUS_OK);
            DIC_EXPECT(codec_basic_decode_image(&streamed, 0,
                                                &streamed_image) ==
                       DIC_STATUS_OK);
            DIC_EXPECT(streamed_image.width == 32);
            DIC_EXPECT(streamed_image.height == 32);
            dic_image_u8_free(&streamed_image);
            codec_basic_encoded_free(&streamed);
        }

        free(buffer);
        buffer = NULL;
        free(stream_bytes);
        fclose(stream);
    }

    /* --- Test 4: Byte-identical to file output --- */
    {
        uint8_t* file_bytes = NULL;
        size_t file_size = 0u;
        FILE* fp = NULL;

        /* Write to file */
        DIC_EXPECT(codec_basic_write_file("__test_serialize.dicw", &encoded) ==
                   DIC_STATUS_OK);

        /* Read file back into memory */
#if defined(_MSC_VER)
        if (fopen_s(&fp, "__test_serialize.dicw", "rb") != 0) fp = NULL;
#else
        fp = fopen("__test_serialize.dicw", "rb");
#endif
        DIC_EXPECT(fp != NULL);
        fseek(fp, 0, SEEK_END);
        file_size = (size_t)ftell(fp);
        fseek(fp, 0, SEEK_SET);
        file_bytes = (uint8_t*)malloc(file_size);
        DIC_EXPECT(file_bytes != NULL);
        DIC_EXPECT(fread(file_bytes, 1u, file_size, fp) == file_size);
        fclose(fp);

        /* Serialize again for comparison */
        {
            uint8_t* buf2 = NULL;
            size_t size2 = 0u;
            DIC_EXPECT(codec_basic_serialize(&encoded, &buf2, &size2) ==
                       DIC_STATUS_OK);
            DIC_EXPECT(size2 == file_size);
            DIC_EXPECT(memcmp(buf2, file_bytes, size2) == 0);
            free(buf2);
        }

        free(file_bytes);
        remove("__test_serialize.dicw");
    }

    /* --- Test 5: Error handling --- */
    {
        codec_basic_encoded_image dec = {0};

        /* Corrupt magic */
        {
            uint8_t bad_magic[32];
            memcpy(bad_magic, "XXXX", 4u);
            DIC_EXPECT(codec_basic_deserialize(bad_magic, sizeof(bad_magic),
                                               &dec) != DIC_STATUS_OK);
        }

        /* Truncated buffer */
        DIC_EXPECT(codec_basic_deserialize(buffer, 4u, &dec) != DIC_STATUS_OK);

        /* NULL buffer */
        DIC_EXPECT(codec_basic_deserialize(NULL, 100u, &dec) != DIC_STATUS_OK);
    }

    codec_basic_encoded_free(&encoded);
    return 0;
}

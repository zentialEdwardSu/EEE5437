#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "codec/metrics.h"
#include "test_helpers.h"

int main(void)
{
    uint8_t source[32 * 32];
    codec_basic_encoded_image encoded = {0};
    uint8_t *buffer = NULL;
    size_t buffer_size = 0u;
    int y, x;

    /* Build test image */
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            source[(size_t)y * 32u + (size_t)x] = (uint8_t)(20 + x * 2 + y * 3 + ((x + y) % 7));

    /* Encode */
    DIC_EXPECT(codec_basic_encode_image(source, 32, 32, 1, 3, 4, &encoded) == DIC_STATUS_OK);

    /* --- Test 1: Serialize roundtrip --- */
    DIC_EXPECT(codec_basic_serialize(&encoded, &buffer, &buffer_size) == DIC_STATUS_OK);
    DIC_EXPECT(buffer != NULL);
    DIC_EXPECT(buffer_size > 28u); /* header is 28 bytes */

    /* Deserialize */
    {
        codec_basic_encoded_image deserialized = {0};
        dic_image_u8 decoded = {0};
        double psnr;

        DIC_EXPECT(codec_basic_deserialize(buffer, buffer_size, &deserialized) == DIC_STATUS_OK);
        DIC_EXPECT(deserialized.width == 32);
        DIC_EXPECT(deserialized.height == 32);
        DIC_EXPECT(deserialized.channels == 1);
        DIC_EXPECT(deserialized.levels == 3);
        DIC_EXPECT(deserialized.quant_step == 4);

        /* Full decode from deserialized data */
        DIC_EXPECT(codec_basic_decode_image(&deserialized, 3, 0, &decoded) == DIC_STATUS_OK);
        DIC_EXPECT(decoded.width == 32);
        DIC_EXPECT(decoded.height == 32);
        psnr = codec_metric_psnr_u8(source, decoded.data, 32u * 32u);
        DIC_EXPECT(psnr > 25.0);

        /* Resolution scalability on deserialized data */
        {
            int expected_sizes[] = {4, 8, 16, 32};
            int res;
            for (res = 0; res <= deserialized.levels; ++res) {
                dic_image_u8 dec_res = {0};
                DIC_EXPECT(codec_basic_decode_image(&deserialized, res, 0, &dec_res) == DIC_STATUS_OK);
                DIC_EXPECT(dec_res.width == expected_sizes[res]);
                DIC_EXPECT(dec_res.height == expected_sizes[res]);
                dic_image_u8_free(&dec_res);
            }
        }

        dic_image_u8_free(&decoded);
        codec_basic_encoded_free(&deserialized);
    }

    free(buffer);
    buffer = NULL;

    /* --- Test 2: Byte-identical to file output --- */
    {
        uint8_t *file_bytes = NULL;
        size_t file_size = 0u;
        FILE *fp = NULL;

        /* Write to file */
        DIC_EXPECT(codec_basic_write_file("__test_serialize.dicw", &encoded) == DIC_STATUS_OK);

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
        file_bytes = (uint8_t *)malloc(file_size);
        DIC_EXPECT(file_bytes != NULL);
        DIC_EXPECT(fread(file_bytes, 1u, file_size, fp) == file_size);
        fclose(fp);

        /* Serialize again for comparison */
        {
            uint8_t *buf2 = NULL;
            size_t size2 = 0u;
            DIC_EXPECT(codec_basic_serialize(&encoded, &buf2, &size2) == DIC_STATUS_OK);
            DIC_EXPECT(size2 == file_size);
            DIC_EXPECT(memcmp(buf2, file_bytes, size2) == 0);
            free(buf2);
        }

        free(file_bytes);
        remove("__test_serialize.dicw");
    }

    /* --- Test 3: Error handling --- */
    {
        codec_basic_encoded_image dec = {0};

        /* Corrupt magic */
        {
            uint8_t bad_magic[32];
            memcpy(bad_magic, "XXXX", 4u);
            DIC_EXPECT(codec_basic_deserialize(bad_magic, sizeof(bad_magic), &dec) != DIC_STATUS_OK);
        }

        /* Truncated buffer */
        DIC_EXPECT(codec_basic_deserialize(buffer, 4u, &dec) != DIC_STATUS_OK);

        /* NULL buffer */
        DIC_EXPECT(codec_basic_deserialize(NULL, 100u, &dec) != DIC_STATUS_OK);
    }

    codec_basic_encoded_free(&encoded);
    return 0;
}

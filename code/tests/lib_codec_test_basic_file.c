#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "test_helpers.h"

#define TEST_FILE "test_resolution.dicw"

int main(void)
{
    uint8_t source[32 * 32];
    int y, x;

    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            source[(size_t)y * 32u + (size_t)x] = (uint8_t)(50 + x + y);

    /* Encode and write */
    {
        codec_basic_encoded_image encoded = {0};
        DIC_EXPECT(codec_basic_encode_image(source, 32, 32, 1, 3, 4, &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(codec_basic_write_file(TEST_FILE, &encoded) == DIC_STATUS_OK);
        codec_basic_encoded_free(&encoded);
    }

    /* Full read and decode */
    {
        codec_basic_encoded_image encoded = {0};
        dic_image_u8 decoded = {0};

        DIC_EXPECT(codec_basic_read_file(TEST_FILE, &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(encoded.levels == 3);
        DIC_EXPECT(codec_basic_decode_image(&encoded, 3, 0, &decoded) == DIC_STATUS_OK);
        DIC_EXPECT(decoded.width == 32);
        DIC_EXPECT(decoded.height == 32);

        codec_basic_encoded_free(&encoded);
        dic_image_u8_free(&decoded);
    }

    /* Partial read — resolution 1 only (LL + 1 high-pass level => 8x8) */
    {
        codec_basic_encoded_image encoded = {0};
        dic_image_u8 decoded = {0};

        DIC_EXPECT(codec_basic_read_file_resolution(TEST_FILE, 1, &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(encoded.channel_streams[0].num_resolutions == 4);
        /* Resolutions 0 and 1 should have data */
        DIC_EXPECT(encoded.channel_streams[0].resolutions[0].num_bitplanes > 0);
        DIC_EXPECT(encoded.channel_streams[0].resolutions[1].num_bitplanes > 0);
        /* Resolutions 2 and 3 should be empty (skipped) */
        DIC_EXPECT(encoded.channel_streams[0].resolutions[2].num_bitplanes == 0);
        DIC_EXPECT(encoded.channel_streams[0].resolutions[3].num_bitplanes == 0);

        DIC_EXPECT(codec_basic_decode_image(&encoded, 1, 0, &decoded) == DIC_STATUS_OK);
        DIC_EXPECT(decoded.width == 8);
        DIC_EXPECT(decoded.height == 8);

        codec_basic_encoded_free(&encoded);
        dic_image_u8_free(&decoded);
    }

    /* Partial read — resolution 0 (LL only => 4x4) */
    {
        codec_basic_encoded_image encoded = {0};
        dic_image_u8 decoded = {0};

        DIC_EXPECT(codec_basic_read_file_resolution(TEST_FILE, 0, &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(codec_basic_decode_image(&encoded, 0, 0, &decoded) == DIC_STATUS_OK);
        DIC_EXPECT(decoded.width == 4);
        DIC_EXPECT(decoded.height == 4);

        codec_basic_encoded_free(&encoded);
        dic_image_u8_free(&decoded);
    }

    /* Clean up */
    remove(TEST_FILE);
    return 0;
}

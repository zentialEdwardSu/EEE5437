#include <stdint.h>
#include <stdio.h>

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "test_helpers.h"

#define TEST_FILE "test_basic_v7.dicw"

int main(void) {
    uint8_t source[32 * 32];
    codec_basic_encoded_image encoded = {0};
    codec_basic_encoded_image readback = {0};
    dic_image_u8 decoded = {0};
    FILE* file;
    int x, y;

    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            source[(size_t)y * 32u + (size_t)x] = (uint8_t)(50 + x + y);

    DIC_EXPECT(codec_basic_encode_image(source, 32, 32, 1, 3, 4.0f,
                                        &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(codec_basic_write_file(TEST_FILE, &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(codec_basic_read_file(TEST_FILE, &readback) == DIC_STATUS_OK);
    DIC_EXPECT(readback.channel_streams[0].num_bitplanes ==
               encoded.channel_streams[0].num_bitplanes);
    DIC_EXPECT(codec_basic_decode_image(&readback, 0, &decoded) ==
               DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == 32 && decoded.height == 32);

    dic_image_u8_free(&decoded);
    codec_basic_encoded_free(&readback);

    file = fopen(TEST_FILE, "r+b");
    DIC_EXPECT(file != NULL);
    DIC_EXPECT(fseek(file, 4, SEEK_SET) == 0);
    DIC_EXPECT(fputc(6, file) != EOF);
    DIC_EXPECT(fputc(0, file) != EOF);
    DIC_EXPECT(fputc(0, file) != EOF);
    DIC_EXPECT(fputc(0, file) != EOF);
    fclose(file);
    DIC_EXPECT(codec_basic_read_file(TEST_FILE, &readback) != DIC_STATUS_OK);

    codec_basic_encoded_free(&encoded);
    remove(TEST_FILE);
    return 0;
}

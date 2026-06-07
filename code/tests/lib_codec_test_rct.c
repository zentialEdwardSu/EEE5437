#include <stdio.h>
#include <string.h>

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "codec/rct.h"
#include "test_helpers.h"

int main(void) {
    {
        int32_t r[] = {10, 255, 0, 128, 7, 200};
        int32_t g[] = {20, 0, 255, 64, 7, 150};
        int32_t b[] = {30, 128, 1, 192, 7, 100};
        int32_t original_r[6], original_g[6], original_b[6];
        memcpy(original_r, r, sizeof(r));
        memcpy(original_g, g, sizeof(g));
        memcpy(original_b, b, sizeof(b));
        DIC_EXPECT(codec_rct_forward(r, g, b, 6) == DIC_STATUS_OK);
        DIC_EXPECT(codec_rct_inverse(r, g, b, 6) == DIC_STATUS_OK);
        DIC_EXPECT(memcmp(r, original_r, sizeof(r)) == 0);
        DIC_EXPECT(memcmp(g, original_g, sizeof(g)) == 0);
        DIC_EXPECT(memcmp(b, original_b, sizeof(b)) == 0);
    }

    {
        enum { W = 16, H = 16, CH = 3 };
        uint8_t source[W * H * CH];
        codec_basic_encoded_image encoded = {0}, readback = {0};
        dic_image_u8 decoded = {0};
        const char* path = "test_forced_rct.bit";
        int x, y;
        for (y = 0; y < H; ++y)
            for (x = 0; x < W; ++x) {
                size_t p = ((size_t)y * W + (size_t)x) * CH;
                source[p] = (uint8_t)(x * 17 + y * 3);
                source[p + 1] = (uint8_t)(x * 5 + y * 11);
                source[p + 2] = (uint8_t)(x * 7 + y * 9);
            }

        DIC_EXPECT(codec_basic_encode_image(source, W, H, CH, 2, 1.0f,
                                            &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(encoded.channels == CH);
        DIC_EXPECT(codec_basic_write_file(path, &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(codec_basic_read_file(path, &readback) == DIC_STATUS_OK);
        DIC_EXPECT(codec_basic_decode_image(&readback, 0, &decoded) ==
                   DIC_STATUS_OK);
        DIC_EXPECT(memcmp(source, decoded.data, sizeof(source)) == 0);

        codec_basic_encoded_free(&encoded);
        codec_basic_encoded_free(&readback);
        dic_image_u8_free(&decoded);
        remove(path);
    }

    {
        uint8_t gray[64];
        codec_basic_encoded_image encoded = {0};
        dic_image_u8 decoded = {0};
        int i;
        for (i = 0; i < 64; ++i) gray[i] = (uint8_t)(i * 4);
        DIC_EXPECT(codec_basic_encode_image(gray, 8, 8, 1, 2, 1.0f,
                                            &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(codec_basic_decode_image(&encoded, 0, &decoded) ==
                   DIC_STATUS_OK);
        DIC_EXPECT(memcmp(gray, decoded.data, sizeof(gray)) == 0);
        dic_image_u8_free(&decoded);
        codec_basic_encoded_free(&encoded);
    }
    return 0;
}

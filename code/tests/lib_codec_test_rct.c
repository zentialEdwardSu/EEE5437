#include <stdio.h>
#include <string.h>

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "codec/rct.h"
#include "image_u8/image_u8.h"
#include "test_helpers.h"

int main(void) {
    /* ── Test 1: Roundtrip bit-exactness (random values) ── */
    {
        int32_t r[] = {10, 255, 0, 128, 7, 200};
        int32_t g[] = {20, 0, 255, 64, 7, 150};
        int32_t b[] = {30, 128, 1, 192, 7, 100};
        int32_t r_orig[6], g_orig[6], b_orig[6];
        size_t n = 6;

        memcpy(r_orig, r, sizeof(r));
        memcpy(g_orig, g, sizeof(g));
        memcpy(b_orig, b, sizeof(b));

        DIC_EXPECT(codec_rct_forward(r, g, b, n) == DIC_STATUS_OK);
        /* Values must actually change */
        DIC_EXPECT(memcmp(r, r_orig, sizeof(r)) != 0);

        DIC_EXPECT(codec_rct_inverse(r, g, b, n) == DIC_STATUS_OK);
        /* Bit-exact roundtrip */
        DIC_EXPECT(memcmp(r, r_orig, sizeof(r)) == 0);
        DIC_EXPECT(memcmp(g, g_orig, sizeof(g)) == 0);
        DIC_EXPECT(memcmp(b, b_orig, sizeof(b)) == 0);
    }

    /* ── Test 2: NULL pointer rejection ── */
    {
        int32_t dummy = 0;
        DIC_EXPECT(codec_rct_forward(NULL, &dummy, &dummy, 1) != DIC_STATUS_OK);
        DIC_EXPECT(codec_rct_forward(&dummy, NULL, &dummy, 1) != DIC_STATUS_OK);
        DIC_EXPECT(codec_rct_forward(&dummy, &dummy, NULL, 1) != DIC_STATUS_OK);
        DIC_EXPECT(codec_rct_inverse(NULL, &dummy, &dummy, 1) != DIC_STATUS_OK);
    }

    /* ── Test 3: Flat gray (R=G=B=128 → Cb=Cr=0) ── */
    {
        int32_t rr = 128, gg = 128, bb = 128;
        DIC_EXPECT(codec_rct_forward(&rr, &gg, &bb, 1) == DIC_STATUS_OK);
        DIC_EXPECT(rr == 128); /* Y = floor((128+256+128)/4) = 128 */
        DIC_EXPECT(gg == 0);   /* Cb = 128 - 128 = 0 */
        DIC_EXPECT(bb == 0);   /* Cr = 128 - 128 = 0 */
    }

    /* ── Test 4: All-zero input ── */
    {
        int32_t rr = 0, gg = 0, bb = 0;
        DIC_EXPECT(codec_rct_forward(&rr, &gg, &bb, 1) == DIC_STATUS_OK);
        DIC_EXPECT(rr == 0); /* Y = 0 */
        DIC_EXPECT(gg == 0); /* Cb = 0 */
        DIC_EXPECT(bb == 0); /* Cr = 0 */
        DIC_EXPECT(codec_rct_inverse(&rr, &gg, &bb, 1) == DIC_STATUS_OK);
        DIC_EXPECT(rr == 0 && gg == 0 && bb == 0);
    }

    /* ── Test 5: encode/decode roundtrip with RCT ── */
    {
        /* Small 16x16 RGB test image */
        enum { W = 16, H = 16, CH = 3 };
        uint8_t source[W * H * CH];
        int x, y;
        for (y = 0; y < H; ++y)
            for (x = 0; x < W; ++x) {
                size_t p = ((size_t)y * W + (size_t)x) * CH;
                source[p + 0] = (uint8_t)((x * 17 + y * 13) % 256); /* R */
                source[p + 1] = (uint8_t)((x * 7 + y * 23) % 256);  /* G */
                source[p + 2] = (uint8_t)((x * 11 + y * 19) % 256); /* B */
            }

        codec_basic_encoded_image encoded = {0};
        DIC_EXPECT(codec_basic_encode_image(
                       source, W, H, CH, /*levels=*/2, /*quant_step=*/1,
                       /*color_transform=*/1, &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(encoded.color_transform == 1);
        DIC_EXPECT(encoded.channels == 3);

        dic_image_u8 decoded = {0};
        DIC_EXPECT(codec_basic_decode_image(&encoded, /*max_res=*/2,
                                            /*num_bp=*/0,
                                            &decoded) == DIC_STATUS_OK);
        DIC_EXPECT(decoded.width == W);
        DIC_EXPECT(decoded.height == H);
        DIC_EXPECT(decoded.channels == CH);

        /* Verify roundtrip (should be exact with all bitplanes) */
        size_t n = (size_t)W * H * CH;
        DIC_EXPECT(memcmp(source, decoded.data, n) == 0);

        codec_basic_encoded_free(&encoded);
        dic_image_u8_free(&decoded);
    }

    /* ── Test 6: RCT file write/read roundtrip ── */
    {
        enum { W = 8, H = 8, CH = 3 };
        uint8_t source[W * H * CH];
        int x, y;
        for (y = 0; y < H; ++y)
            for (x = 0; x < W; ++x) {
                size_t p = ((size_t)y * W + (size_t)x) * CH;
                source[p + 0] = (uint8_t)(x * 31);
                source[p + 1] = (uint8_t)(y * 17);
                source[p + 2] = (uint8_t)((x + y) * 13);
            }

        codec_basic_encoded_image encoded = {0}, readback = {0};
        DIC_EXPECT(codec_basic_encode_image(source, W, H, CH, 1, 1, 1,
                                            &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(encoded.color_transform == 1);

        const char* path = "test_rct_file.bit";
        DIC_EXPECT(codec_basic_write_file(path, &encoded) == DIC_STATUS_OK);
        DIC_EXPECT(codec_basic_read_file(path, &readback) == DIC_STATUS_OK);
        DIC_EXPECT(readback.color_transform == 1);
        DIC_EXPECT(readback.width == W);
        DIC_EXPECT(readback.channels == CH);

        dic_image_u8 decoded = {0};
        DIC_EXPECT(codec_basic_decode_image(&readback, 1, 0, &decoded) ==
                   DIC_STATUS_OK);
        DIC_EXPECT(memcmp(source, decoded.data, (size_t)W * H * CH) == 0);

        codec_basic_encoded_free(&encoded);
        codec_basic_encoded_free(&readback);
        dic_image_u8_free(&decoded);
        remove(path);
    }

    /* ── Test 7: Grayscale with color_transform=0 still works ── */
    {
        uint8_t gray[64] = {0};
        int i;
        for (i = 0; i < 64; ++i) gray[i] = (uint8_t)(i * 4);
        codec_basic_encoded_image encoded = {0};
        DIC_EXPECT(codec_basic_encode_image(gray, 8, 8, 1, 2, 2, 0, &encoded) ==
                   DIC_STATUS_OK);
        DIC_EXPECT(encoded.color_transform == 0);
        codec_basic_encoded_free(&encoded);
    }

    /* ── Test 8: color_transform=1 rejected for grayscale ── */
    {
        uint8_t gray[64] = {0};
        codec_basic_encoded_image encoded = {0};
        DIC_EXPECT(codec_basic_encode_image(gray, 8, 8, 1, 2, 2, 1, &encoded) !=
                   DIC_STATUS_OK);
    }

    return 0;
}

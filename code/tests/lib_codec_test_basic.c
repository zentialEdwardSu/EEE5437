#include <stdint.h>

#include "codec/basic_codec.h"
#include "codec/metrics.h"
#include "test_helpers.h"

int main(void) {
    uint8_t source[32 * 32];
    codec_basic_encoded_image encoded = {0};
    int y, x, res;

    /* Build test image */
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            source[(size_t)y * 32u + (size_t)x] =
                (uint8_t)(20 + x * 2 + y * 3 + ((x + y) % 7));

    /* Encode */
    DIC_EXPECT(codec_basic_encode_image(source, 32, 32, 1, 3, 4, 0, &encoded) ==
               DIC_STATUS_OK);
    DIC_EXPECT(encoded.width == 32);
    DIC_EXPECT(encoded.height == 32);
    DIC_EXPECT(encoded.channels == 1);
    DIC_EXPECT(encoded.levels == 3);
    DIC_EXPECT(encoded.channel_streams[0].num_resolutions == 4);

    /* Full decode (max_resolution=3, all bitplanes=0) */
    {
        dic_image_u8 decoded = {0};
        double psnr;

        DIC_EXPECT(codec_basic_decode_image(&encoded, 3, 0, &decoded) ==
                   DIC_STATUS_OK);
        DIC_EXPECT(decoded.width == 32);
        DIC_EXPECT(decoded.height == 32);

        psnr = codec_metric_psnr_u8(source, decoded.data, 32u * 32u);
        DIC_EXPECT(psnr > 25.0);
        dic_image_u8_free(&decoded);
    }

    /* Resolution scalability: each level produces correct size */
    {
        int expected_sizes[] = {4, 8, 16, 32};

        for (res = 0; res <= encoded.levels; ++res) {
            dic_image_u8 decoded = {0};
            DIC_EXPECT(codec_basic_decode_image(&encoded, res, 0, &decoded) ==
                       DIC_STATUS_OK);
            DIC_EXPECT(decoded.width == expected_sizes[res]);
            DIC_EXPECT(decoded.height == expected_sizes[res]);
            DIC_EXPECT(decoded.channels == 1);
            dic_image_u8_free(&decoded);
        }
    }

    /* Quality scalability at full resolution: PSNR monotonically increases */
    {
        double psnr_prev = -1.0;
        int max_bp = 0;
        int bp;

        /* Find max bitplanes across all resolutions */
        for (res = 0; res <= encoded.levels; ++res) {
            int n = encoded.channel_streams[0].resolutions[res].num_bitplanes;
            if (n > max_bp) max_bp = n;
        }

        for (bp = 1; bp <= max_bp; ++bp) {
            dic_image_u8 decoded = {0};
            double psnr_prog;

            DIC_EXPECT(codec_basic_decode_image(&encoded, encoded.levels, bp,
                                                &decoded) == DIC_STATUS_OK);
            psnr_prog = codec_metric_psnr_u8(source, decoded.data, 32u * 32u);
            DIC_EXPECT(psnr_prog >= psnr_prev - 0.01);
            psnr_prev = psnr_prog;
            dic_image_u8_free(&decoded);
        }
    }

    codec_basic_encoded_free(&encoded);
    return 0;
}

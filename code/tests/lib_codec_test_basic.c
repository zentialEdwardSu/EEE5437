#include <stdint.h>

#include "codec/basic_codec.h"
#include "codec/metrics.h"
#include "test_helpers.h"

int main(void) {
    uint8_t source[32 * 32];
    codec_basic_encoded_image encoded = {0};
    double previous_psnr = -1.0;
    int x, y, bp;

    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            source[(size_t)y * 32u + (size_t)x] =
                (uint8_t)(20 + x * 2 + y * 3 + ((x + y) % 7));

    DIC_EXPECT(codec_basic_encode_image(source, 32, 32, 1, 3, 4.0f,
                                        &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(encoded.width == 32);
    DIC_EXPECT(encoded.height == 32);
    DIC_EXPECT(encoded.channels == 1);
    DIC_EXPECT(encoded.channel_streams[0].num_bitplanes > 0);

    for (bp = 1; bp <= encoded.channel_streams[0].num_bitplanes; ++bp) {
        dic_image_u8 decoded = {0};
        double psnr;
        DIC_EXPECT(codec_basic_decode_image(&encoded, bp, &decoded) ==
                   DIC_STATUS_OK);
        DIC_EXPECT(decoded.width == 32);
        DIC_EXPECT(decoded.height == 32);
        psnr = codec_metric_psnr_u8(source, decoded.data, 32u * 32u);
        DIC_EXPECT(psnr >= previous_psnr - 0.01);
        previous_psnr = psnr;
        dic_image_u8_free(&decoded);
    }

    {
        dic_image_u8 decoded = {0};
        DIC_EXPECT(codec_basic_decode_image(&encoded, 0, &decoded) ==
                   DIC_STATUS_OK);
        DIC_EXPECT(codec_metric_psnr_u8(source, decoded.data, 32u * 32u) >
                   25.0);
        dic_image_u8_free(&decoded);
    }

    codec_basic_encoded_free(&encoded);
    return 0;
}

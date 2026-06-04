#include <stdint.h>

#include "codec/basic_codec.h"
#include "codec/metrics.h"
#include "test_helpers.h"

int main(void)
{
    uint8_t source[16 * 16];
    codec_basic_encoded_image encoded = {0};
    dic_image_u8 decoded = {0};
    double psnr, psnr_prev;
    int y, x, bp;

    for (y = 0; y < 16; ++y)
        for (x = 0; x < 16; ++x)
            source[(y * 16) + x] = (uint8_t)(20 + x * 8 + y * 3 + ((x + y) % 5));

    DIC_EXPECT(codec_basic_encode_image(source, 16, 16, 1, 3, 4, &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(encoded.width == 16);
    DIC_EXPECT(encoded.height == 16);
    DIC_EXPECT(encoded.channels == 1);
    DIC_EXPECT(encoded.num_bitplanes > 0);

    /* Full decode */
    DIC_EXPECT(codec_basic_decode_image(&encoded, &decoded) == DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == 16);
    DIC_EXPECT(decoded.height == 16);
    DIC_EXPECT(decoded.channels == 1);

    psnr = codec_metric_psnr_u8(source, decoded.data, 16u * 16u);
    DIC_EXPECT(psnr > 25.0);

    dic_image_u8_free(&decoded);

    /* Progressive decode: PSNR should increase with more bitplanes */
    psnr_prev = -1.0;
    for (bp = 1; bp <= encoded.num_bitplanes; ++bp) {
        dic_image_u8 prog = {0};
        double psnr_prog;
        DIC_EXPECT(codec_basic_decode_image_bitplanes(&encoded, bp, &prog) == DIC_STATUS_OK);
        psnr_prog = codec_metric_psnr_u8(source, prog.data, 16u * 16u);
        DIC_EXPECT(psnr_prog >= psnr_prev - 0.01);
        psnr_prev = psnr_prog;
        dic_image_u8_free(&prog);
    }

    codec_basic_encoded_free(&encoded);
    return 0;
}

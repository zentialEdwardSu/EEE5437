#include <stdio.h>

#include "codec/dic_metrics.h"
#include "codec/dic_snr_codec.h"
#include "test_helpers.h"

int main(void)
{
    const char *path = "dic_snr_test.dics";
    uint8_t source[32 * 32];
    dic_snr_encoded_image encoded = {0};
    dic_snr_encoded_image read_back = {0};
    dic_image_u8 full = {0};
    dic_image_u8 partial = {0};
    double full_psnr;
    double partial_psnr;
    size_t full_bits;
    size_t partial_bits;
    int y;
    int x;
    int max_bitplanes;

    for (y = 0; y < 32; ++y)
    {
        for (x = 0; x < 32; ++x)
            source[(y * 32) + x] = (uint8_t)((x * 5 + y * 3 + ((x * y) % 17)) % 256);
    }

    DIC_EXPECT(dic_snr_encode_image(source, 32, 32, 1, 4, 4, &encoded) == DIC_STATUS_OK);
    max_bitplanes = dic_snr_max_bitplanes(&encoded);
    DIC_EXPECT(max_bitplanes > 4);
    full_bits = dic_snr_layer_bit_count(&encoded, 0);
    partial_bits = dic_snr_layer_bit_count(&encoded, 3);
    DIC_EXPECT(full_bits > partial_bits);
    DIC_EXPECT(partial_bits > 0u);

    DIC_EXPECT(dic_snr_decode_image(&encoded, 0, &full) == DIC_STATUS_OK);
    DIC_EXPECT(dic_snr_decode_image(&encoded, 3, &partial) == DIC_STATUS_OK);
    full_psnr = dic_metric_psnr_u8(source, full.data, sizeof(source));
    partial_psnr = dic_metric_psnr_u8(source, partial.data, sizeof(source));
    DIC_EXPECT(full_psnr > partial_psnr);
    DIC_EXPECT(partial_psnr > 5.0);

    DIC_EXPECT(dic_snr_write_file(path, &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(dic_snr_read_file(path, &read_back) == DIC_STATUS_OK);
    DIC_EXPECT(read_back.width == encoded.width);
    DIC_EXPECT(read_back.height == encoded.height);
    DIC_EXPECT(read_back.channels == encoded.channels);
    DIC_EXPECT(dic_snr_max_bitplanes(&read_back) == max_bitplanes);

    dic_image_u8_free(&full);
    dic_image_u8_free(&partial);
    dic_snr_encoded_free(&read_back);
    dic_snr_encoded_free(&encoded);
    remove(path);
    return 0;
}

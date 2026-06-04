#include <stdio.h>

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "codec/metrics.h"
#include "test_helpers.h"

static long dic_test_file_size(const char *path)
{
    FILE *file = NULL;
    long size = -1;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0)
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) return -1;
    if (fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    fclose(file);
    return size;
}

int main(void)
{
    const char *path = "codec_basic_test.dicw";
    uint8_t source[16 * 16];
    codec_basic_encoded_image encoded = {0};
    codec_basic_encoded_image read_back = {0};
    dic_image_u8 decoded_mem = {0};
    dic_image_u8 decoded_file = {0};
    double psnr_mem, psnr_file;
    long file_size;
    int y, x;

    for (y = 0; y < 16; ++y)
        for (x = 0; x < 16; ++x)
            source[(y * 16) + x] = (uint8_t)(20 + x * 8 + y * 3 + ((x + y) % 5));

    DIC_EXPECT(codec_basic_encode_image(source, 16, 16, 1, 3, 4, &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(encoded.num_bitplanes > 0);

    /* In-memory roundtrip */
    DIC_EXPECT(codec_basic_decode_image(&encoded, &decoded_mem) == DIC_STATUS_OK);
    psnr_mem = codec_metric_psnr_u8(source, decoded_mem.data, 16u * 16u);

    /* File roundtrip */
    DIC_EXPECT(codec_basic_write_file(path, &encoded) == DIC_STATUS_OK);
    file_size = dic_test_file_size(path);
    DIC_EXPECT(file_size > 0);

    DIC_EXPECT(codec_basic_read_file(path, &read_back) == DIC_STATUS_OK);
    DIC_EXPECT(read_back.width == encoded.width);
    DIC_EXPECT(read_back.height == encoded.height);
    DIC_EXPECT(read_back.channels == encoded.channels);
    DIC_EXPECT(read_back.levels == encoded.levels);
    DIC_EXPECT(read_back.quant_step == encoded.quant_step);
    DIC_EXPECT(read_back.num_bitplanes == encoded.num_bitplanes);

    DIC_EXPECT(codec_basic_decode_image(&read_back, &decoded_file) == DIC_STATUS_OK);
    psnr_file = codec_metric_psnr_u8(source, decoded_file.data, 16u * 16u);

    DIC_EXPECT(psnr_mem > 25.0);
    DIC_EXPECT(psnr_file > 25.0);
    DIC_EXPECT(psnr_file > psnr_mem - 0.1);

    dic_image_u8_free(&decoded_mem);
    dic_image_u8_free(&decoded_file);
    codec_basic_encoded_free(&read_back);

    /* Progressive file read */
    {
        codec_basic_encoded_image partial = {0};
        dic_image_u8 partial_decoded = {0};
        double partial_psnr;

        DIC_EXPECT(codec_basic_read_file_bitplanes(path, 1, &partial) == DIC_STATUS_OK);
        DIC_EXPECT(partial.num_bitplanes == 1);
        DIC_EXPECT(codec_basic_decode_image(&partial, &partial_decoded) == DIC_STATUS_OK);
        partial_psnr = codec_metric_psnr_u8(source, partial_decoded.data, 16u * 16u);
        printf("  full PSNR=%.4f, 1-bp PSNR=%.4f\n", psnr_file, partial_psnr);
        DIC_EXPECT(partial_psnr < psnr_file);
        DIC_EXPECT(partial_psnr > 5.0);

        dic_image_u8_free(&partial_decoded);
        codec_basic_encoded_free(&partial);
    }

    codec_basic_encoded_free(&encoded);
    remove(path);
    return 0;
}

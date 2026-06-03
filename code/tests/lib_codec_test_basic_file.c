#include <stdio.h>

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
    if (file == NULL)
        return -1;
    if (fseek(file, 0, SEEK_END) == 0)
        size = ftell(file);
    fclose(file);
    return size;
}

int main(void)
{
    const char *path = "codec_basic_test.dicw";
    uint8_t source[16 * 16 * 3];
    codec_basic_encoded_image encoded = {0};
    codec_basic_encoded_image read_back = {0};
    dic_image_u8 decoded = {0};
    double psnr;
    long file_size;
    int y;
    int x;
    int c;

    for (y = 0; y < 16; ++y)
    {
        for (x = 0; x < 16; ++x)
        {
            for (c = 0; c < 3; ++c)
            {
                source[(((y * 16) + x) * 3) + c] =
                    (uint8_t)(13 + x * 5 + y * 7 + c * 31 + ((x + c) % 4));
            }
        }
    }

    DIC_EXPECT(codec_basic_encode_image(source, 16, 16, 3, 3, 4, &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(codec_basic_write_file(path, &encoded) == DIC_STATUS_OK);
    file_size = dic_test_file_size(path);
    DIC_EXPECT(file_size > 0);

    DIC_EXPECT(codec_basic_read_file(path, &read_back) == DIC_STATUS_OK);
    DIC_EXPECT(read_back.width == encoded.width);
    DIC_EXPECT(read_back.height == encoded.height);
    DIC_EXPECT(read_back.channels == encoded.channels);
    DIC_EXPECT(read_back.levels == encoded.levels);
    DIC_EXPECT(read_back.quant_step == encoded.quant_step);
    DIC_EXPECT(codec_basic_symbol_count(&read_back) == codec_basic_symbol_count(&encoded));

    DIC_EXPECT(codec_basic_decode_image(&read_back, &decoded) == DIC_STATUS_OK);
    psnr = codec_metric_psnr_u8(source, decoded.data, sizeof(source));
    DIC_EXPECT(psnr > 24.0);

    dic_image_u8_free(&decoded);
    codec_basic_encoded_free(&read_back);
    codec_basic_encoded_free(&encoded);
    remove(path);
    return 0;
}

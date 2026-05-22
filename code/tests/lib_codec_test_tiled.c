#include <stdio.h>

#include "codec/dic_metrics.h"
#include "codec/dic_tiled_codec.h"
#include "test_helpers.h"

int main(void)
{
    const char *path = "dic_tiled_test.dict";
    const int width = 37;
    const int height = 29;
    uint8_t source[37 * 29 * 3];
    dic_tiled_encoded_image encoded = {0};
    dic_tiled_encoded_image read_back = {0};
    dic_image_u8 decoded = {0};
    double psnr;
    int y;
    int x;
    int c;

    for (y = 0; y < height; ++y)
    {
        for (x = 0; x < width; ++x)
        {
            for (c = 0; c < 3; ++c)
            {
                source[(((y * width) + x) * 3) + c] =
                    (uint8_t)((x * 7 + y * 5 + c * 41 + ((x * y) % 11)) % 256);
            }
        }
    }

    DIC_EXPECT(dic_tiled_encode_image(source, width, height, 3, 4, 5, 16, 16, &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(encoded.tile_count == 6);
    DIC_EXPECT(encoded.tiles[0].encoded.levels == 4);
    DIC_EXPECT(encoded.tiles[2].width == 5);
    DIC_EXPECT(encoded.tiles[2].encoded.levels < 4);

    DIC_EXPECT(dic_tiled_decode_image(&encoded, &decoded) == DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == width);
    DIC_EXPECT(decoded.height == height);
    DIC_EXPECT(decoded.channels == 3);
    psnr = dic_metric_psnr_u8(source, decoded.data, sizeof(source));
    DIC_EXPECT(psnr > 22.0);
    dic_image_u8_free(&decoded);

    DIC_EXPECT(dic_tiled_write_file(path, &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(dic_tiled_read_file(path, &read_back) == DIC_STATUS_OK);
    DIC_EXPECT(read_back.width == encoded.width);
    DIC_EXPECT(read_back.height == encoded.height);
    DIC_EXPECT(read_back.channels == encoded.channels);
    DIC_EXPECT(read_back.tile_count == encoded.tile_count);

    DIC_EXPECT(dic_tiled_decode_image(&read_back, &decoded) == DIC_STATUS_OK);
    psnr = dic_metric_psnr_u8(source, decoded.data, sizeof(source));
    DIC_EXPECT(psnr > 22.0);

    dic_image_u8_free(&decoded);
    dic_tiled_encoded_free(&read_back);
    dic_tiled_encoded_free(&encoded);
    remove(path);
    return 0;
}

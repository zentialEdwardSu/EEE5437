#include <stdio.h>

#include "codec/dic_metrics.h"
#include "codec/dic_roi_codec.h"
#include "test_helpers.h"

static double dic_test_region_mse(
    const uint8_t *a,
    const uint8_t *b,
    int width,
    dic_rect_i32 rect
)
{
    double sum = 0.0;
    int count = 0;
    int y;

    for (y = rect.y; y < rect.y + rect.height; ++y)
    {
        int x;

        for (x = rect.x; x < rect.x + rect.width; ++x)
        {
            double delta = (double)a[(y * width) + x] - (double)b[(y * width) + x];
            sum += delta * delta;
            ++count;
        }
    }

    return count > 0 ? sum / (double)count : 0.0;
}

int main(void)
{
    const char *path = "dic_roi_test.dicr";
    uint8_t source[64 * 64];
    dic_roi_encoded_image encoded = {0};
    dic_roi_encoded_image read_back = {0};
    dic_image_u8 partial = {0};
    dic_image_u8 full = {0};
    dic_rect_i32 detected = {0};
    dic_rect_i32 target = {20, 18, 24, 22};
    double roi_partial_mse;
    double roi_full_mse;
    int x;
    int y;

    for (y = 0; y < 64; ++y)
    {
        for (x = 0; x < 64; ++x)
        {
            int value = 30 + ((x + y) % 9);
            if (x >= target.x && x < target.x + target.width
                && y >= target.y && y < target.y + target.height)
            {
                value = ((x + y) % 2) ? 220 : 50;
            }
            source[(y * 64) + x] = (uint8_t)value;
        }
    }

    DIC_EXPECT(dic_roi_detect_auto(source, 64, 64, 1, &detected) == DIC_STATUS_OK);
    DIC_EXPECT(detected.x <= target.x);
    DIC_EXPECT(detected.y <= target.y);
    DIC_EXPECT(detected.x + detected.width >= target.x + target.width);
    DIC_EXPECT(detected.y + detected.height >= target.y + target.height);

    DIC_EXPECT(dic_roi_encode_image(source, 64, 64, 1, 5, 6, &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(encoded.roi_shift == DIC_ROI_DEFAULT_SHIFT);
    DIC_EXPECT(encoded.roi_rect.x == detected.x);
    DIC_EXPECT(encoded.roi_rect.y == detected.y);

    DIC_EXPECT(dic_roi_decode_image(&encoded, 4, &partial) == DIC_STATUS_OK);
    DIC_EXPECT(dic_roi_decode_image(&encoded, 0, &full) == DIC_STATUS_OK);
    roi_partial_mse = dic_test_region_mse(source, partial.data, 64, target);
    roi_full_mse = dic_test_region_mse(source, full.data, 64, target);
    DIC_EXPECT(roi_full_mse <= roi_partial_mse);
    DIC_EXPECT(dic_metric_psnr_u8(source, full.data, sizeof(source)) > 20.0);

    DIC_EXPECT(dic_roi_write_file(path, &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(dic_roi_read_file(path, &read_back) == DIC_STATUS_OK);
    DIC_EXPECT(read_back.roi_rect.x == encoded.roi_rect.x);
    DIC_EXPECT(read_back.roi_rect.y == encoded.roi_rect.y);
    DIC_EXPECT(read_back.roi_rect.width == encoded.roi_rect.width);
    DIC_EXPECT(read_back.roi_rect.height == encoded.roi_rect.height);

    dic_image_u8_free(&partial);
    dic_image_u8_free(&full);
    dic_roi_encoded_free(&read_back);
    dic_roi_encoded_free(&encoded);
    remove(path);
    return 0;
}

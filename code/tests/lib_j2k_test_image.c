#include <stdio.h>

#include "image_u8/image_u8.h"
#include "j2k/dic_j2k_image.h"
#include "j2k/dic_j2k_parse.h"
#include "test_helpers.h"

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G, Annex F, Annex D, and Annex B.10, image samples become packetized EBCOT tile-part payload. */
int main(void)
{
    const char *j2k_path = "dic_image_real_payload_test.j2k";
    const char *jp2_path = "dic_image_real_payload_test.jp2";
    dic_image_u8 gray;
    dic_image_u8 rgb;
    dic_j2k_codestream_info info;
    int x;
    int y;

    dic_image_u8_init(&gray);
    dic_image_u8_init(&rgb);

    DIC_EXPECT(dic_image_u8_alloc(&gray, 8, 8, 1) == DIC_STATUS_OK);
    for (y = 0; y < gray.height; ++y)
    {
        for (x = 0; x < gray.width; ++x)
            gray.data[(size_t)y * (size_t)gray.width + (size_t)x] = (uint8_t)(x * 17 + y * 11);
    }

    DIC_EXPECT(dic_j2k_write_image_codestream(j2k_path, &gray, 5) == DIC_STATUS_OK);
    DIC_EXPECT(dic_j2k_read_codestream_info(j2k_path, &info) == DIC_STATUS_OK);
    DIC_EXPECT(info.params.width == 8u);
    DIC_EXPECT(info.params.height == 8u);
    DIC_EXPECT(info.params.components == 1u);
    DIC_EXPECT(info.params.decomposition_levels == 3u);
    DIC_EXPECT(info.tile_part_payload_bytes > 4u);
    remove(j2k_path);

    DIC_EXPECT(dic_image_u8_alloc(&rgb, 7, 5, 3) == DIC_STATUS_OK);
    for (y = 0; y < rgb.height; ++y)
    {
        for (x = 0; x < rgb.width; ++x)
        {
            size_t pixel = ((size_t)y * (size_t)rgb.width + (size_t)x) * 3u;

            rgb.data[pixel + 0u] = (uint8_t)(x * 31 + y * 3);
            rgb.data[pixel + 1u] = (uint8_t)(x * 5 + y * 23);
            rgb.data[pixel + 2u] = (uint8_t)(x * 13 + y * 19);
        }
    }

    DIC_EXPECT(dic_j2k_write_image_jp2(jp2_path, &rgb, 5) == DIC_STATUS_OK);
    DIC_EXPECT(dic_jp2_read_codestream_info(jp2_path, &info) == DIC_STATUS_OK);
    DIC_EXPECT(info.params.width == 7u);
    DIC_EXPECT(info.params.height == 5u);
    DIC_EXPECT(info.params.components == 3u);
    DIC_EXPECT(info.params.decomposition_levels == 3u);
    DIC_EXPECT(info.params.multiple_component_transform == 1u);
    DIC_EXPECT(info.tile_part_payload_bytes > 8u);
    remove(jp2_path);

    dic_image_u8_free(&gray);
    dic_image_u8_free(&rgb);
    return 0;
}

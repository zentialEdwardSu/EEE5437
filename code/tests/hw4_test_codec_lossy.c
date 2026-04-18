#include <stdint.h>

#include "hw4/hw4_codec.h"
#include "hw4/hw4_preview.h"
#include "test_helpers.h"

int main(void)
{
    uint8_t source[4 * 4 * 3];
    dic_hw4_encoded_image encoded = {0};
    dic_hw4_image_u8 preview = {0};
    dic_hw4_image_u8 decoded = {0};
    int y;
    int x;
    int c;

    for (y = 0; y < 4; ++y)
    {
        for (x = 0; x < 4; ++x)
        {
            for (c = 0; c < 3; ++c)
                source[((y * 4) + x) * 3 + c] = (uint8_t)(20 + x * 17 + y * 13 + c * 19);
        }
    }

    DIC_EXPECT(dic_hw4_encode_image(source, 4, 4, 3, 1, 70, &encoded) == DIC_STATUS_OK);
    DIC_EXPECT(encoded.coefficient_count == 4u * 4u * 3u);
    DIC_EXPECT(dic_hw4_build_preview_image(&encoded, &preview) == DIC_STATUS_OK);
    DIC_EXPECT(preview.width == 4);
    DIC_EXPECT(preview.height == 4);
    DIC_EXPECT(preview.channels == 3);
    DIC_EXPECT(dic_hw4_decode_image(&encoded, &decoded) == DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == 4);
    DIC_EXPECT(decoded.height == 4);
    DIC_EXPECT(decoded.channels == 3);

    for (y = 0; y < decoded.height; ++y)
    {
        for (x = 0; x < decoded.width; ++x)
        {
            for (c = 0; c < decoded.channels; ++c)
            {
                const int delta = (int)decoded.data[((y * decoded.width) + x) * decoded.channels + c]
                    - (int)source[((y * 4) + x) * 3 + c];
                DIC_EXPECT(delta > -80);
                DIC_EXPECT(delta < 80);
            }
        }
    }

    dic_hw4_image_free(&decoded);
    dic_hw4_image_free(&preview);
    dic_hw4_encoded_free(&encoded);
    return 0;
}

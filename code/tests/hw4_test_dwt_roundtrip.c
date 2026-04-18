#include <string.h>

#include "hw4/hw4_jpeg2000.h"
#include "test_helpers.h"

int main(void)
{
    int32_t original[35];
    int32_t transformed[35];
    int y;
    int x;

    for (y = 0; y < 7; ++y)
    {
        for (x = 0; x < 5; ++x)
            original[(y * 5) + x] = (int32_t)(x * 11 + y * 7 + ((x + y) % 3));
    }

    memcpy(transformed, original, sizeof(original));

    DIC_EXPECT(dic_hw4_forward_transform_plane(transformed, 5, 7, 2) == DIC_STATUS_OK);
    DIC_EXPECT(dic_hw4_inverse_transform_plane(transformed, 5, 7, 2) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(transformed, original, sizeof(original)) == 0);

    return 0;
}

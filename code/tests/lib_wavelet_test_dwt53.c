#include <string.h>

#include "test_helpers.h"
#include "wavelet/dic_dwt53.h"

int main(void)
{
    const int32_t standard_input[4 * 4] = {
        3, 8, 13, 18,
        10, 16, 22, 25,
        17, 24, 28, 32,
        24, 29, 34, 39
    };
    const int32_t standard_forward[4 * 4] = {
        3, 15, 0, 4,
        20, 31, 1, 4,
        0, 1, -1, -2,
        7, 6, -1, 1
    };
    int32_t standard_plane[4 * 4];
    int32_t original[9 * 7];
    int32_t plane[9 * 7];
    int y;
    int x;

    memcpy(standard_plane, standard_input, sizeof(standard_plane));
    DIC_EXPECT(dic_dwt53_forward_plane(standard_plane, 4, 4, 1) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(standard_plane, standard_forward, sizeof(standard_forward)) == 0);
    DIC_EXPECT(dic_dwt53_inverse_plane(standard_plane, 4, 4, 1) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(standard_plane, standard_input, sizeof(standard_input)) == 0);

    for (y = 0; y < 7; ++y)
    {
        for (x = 0; x < 9; ++x)
            original[(y * 9) + x] = (int32_t)(3 + x * 5 + y * 9 + ((x * y) % 4));
    }

    memcpy(plane, original, sizeof(plane));
    DIC_EXPECT(dic_dwt53_forward_plane(plane, 9, 7, 2) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(plane, original, sizeof(plane)) != 0);
    DIC_EXPECT(dic_dwt53_inverse_plane(plane, 9, 7, 2) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(plane, original, sizeof(plane)) == 0);
    return 0;
}

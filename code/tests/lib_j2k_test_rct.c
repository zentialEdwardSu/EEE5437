#include <string.h>

#include "j2k/j2k_rct.h"
#include "test_helpers.h"

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G.2.1, RCT is exactly reversible for integer RGB samples. */
int main(void)
{
    int32_t shifted_samples[] = {
        -128, -128, -128,
        127, -128, 0,
        -5, 2, 9
    };
    const int32_t shifted_rct[] = {
        -128, 0, 0,
        -33, 128, 255,
        2, 7, -7
    };
    const int32_t shifted_original[sizeof(shifted_samples) / sizeof(shifted_samples[0])] = {
        -128, -128, -128,
        127, -128, 0,
        -5, 2, 9
    };
    int32_t samples[] = {
        10, 20, 30,
        255, 0, 128,
        7, 7, 7,
        0, 255, 1
    };
    int32_t original[sizeof(samples) / sizeof(samples[0])];

    DIC_EXPECT(j2k_rct_forward(shifted_samples, 3u) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(shifted_samples, shifted_rct, sizeof(shifted_rct)) == 0);
    DIC_EXPECT(j2k_rct_inverse(shifted_samples, 3u) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(shifted_samples, shifted_original, sizeof(shifted_original)) == 0);

    memcpy(original, samples, sizeof(samples));
    DIC_EXPECT(j2k_rct_forward(samples, 4u) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(samples, original, sizeof(samples)) != 0);
    DIC_EXPECT(j2k_rct_inverse(samples, 4u) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(samples, original, sizeof(samples)) == 0);

    return 0;
}

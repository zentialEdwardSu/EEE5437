/**
 * @file lib_j2k_test_ict.c
 * @brief Tests the JPEG 2000 irreversible component transform module.
 */

#include <math.h>
#include <string.h>

#include "j2k/j2k_ict.h"
#include "test_helpers.h"

static void dic_expect_near(double actual, double expected, double tolerance)
{
    DIC_EXPECT(fabs(actual - expected) <= tolerance);
}

int main(void)
{
    double pure_red[] = {1.0, 0.0, 0.0};
    double samples[] = {
        -128.0, -128.0, -128.0,
        127.0, -128.0, 0.0,
        -5.25, 2.5, 9.75,
        0.0, 255.0, 1.0
    };
    double original[sizeof(samples) / sizeof(samples[0])];
    size_t index;

    DIC_EXPECT(j2k_ict_forward(pure_red, 1u) == DIC_STATUS_OK);
    dic_expect_near(pure_red[0], 0.299, 1e-12);
    dic_expect_near(pure_red[1], -0.16875, 1e-12);
    dic_expect_near(pure_red[2], 0.5, 1e-12);

    memcpy(original, samples, sizeof(samples));
    DIC_EXPECT(j2k_ict_forward(samples, 4u) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_ict_inverse(samples, 4u) == DIC_STATUS_OK);
    for (index = 0u; index < sizeof(samples) / sizeof(samples[0]); ++index)
        dic_expect_near(samples[index], original[index], 1e-9);

    DIC_EXPECT(j2k_ict_forward(NULL, 1u) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_ict_inverse(NULL, 1u) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_ict_forward(samples, 0u) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_ict_inverse(samples, 0u) == DIC_STATUS_INVALID_ARGUMENT);

    return 0;
}

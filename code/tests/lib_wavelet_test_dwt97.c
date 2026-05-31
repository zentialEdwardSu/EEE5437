#include <math.h>
#include <string.h>

#include "test_helpers.h"
#include "wavelet/dic_dwt97.h"

static double dic_test_abs(double value)
{
    return value < 0.0 ? -value : value;
}

static double dic_test_max_abs_error(
    const double *actual,
    const double *expected,
    int count
)
{
    double max_error = 0.0;
    int i;

    for (i = 0; i < count; ++i)
    {
        double error = dic_test_abs(actual[i] - expected[i]);
        if (error > max_error)
            max_error = error;
    }

    return max_error;
}

int main(void)
{
    const double changing_input[4 * 4] = {
        3.0, 8.0, 13.0, 18.0,
        10.0, 16.0, 22.0, 25.0,
        17.0, 24.0, 28.0, 32.0,
        24.0, 29.0, 34.0, 39.0
    };
    double changing_plane[4 * 4];
    double original[9 * 7];
    double plane[9 * 7];
    int y;
    int x;

    DIC_EXPECT(dic_dwt97_low_size(9) == 5);
    DIC_EXPECT(dic_dwt97_high_size(9) == 4);
    DIC_EXPECT(dic_dwt97_forward_plane(NULL, 4, 4, 1) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(dic_dwt97_inverse_plane(NULL, 4, 4, 1) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(dic_dwt97_validate_levels(0, 4, 1) == DIC_HW4_INVALID_DIMENSIONS);
    DIC_EXPECT(dic_dwt97_validate_levels(4, 4, 0) == DIC_HW4_INVALID_LEVELS);
    DIC_EXPECT(dic_dwt97_validate_levels(3, 3, 3) == DIC_HW4_INVALID_LEVELS);

    memcpy(changing_plane, changing_input, sizeof(changing_plane));
    DIC_EXPECT(dic_dwt97_forward_plane(changing_plane, 4, 4, 1) == DIC_STATUS_OK);
    DIC_EXPECT(dic_test_max_abs_error(changing_plane, changing_input, 4 * 4) > 1.0);

    for (y = 0; y < 7; ++y)
    {
        for (x = 0; x < 9; ++x)
            original[(y * 9) + x] = 3.0 + x * 5.0 + y * 9.0 + (double)((x * y) % 4);
    }

    memcpy(plane, original, sizeof(plane));
    DIC_EXPECT(dic_dwt97_forward_plane(plane, 9, 7, 2) == DIC_STATUS_OK);
    DIC_EXPECT(dic_test_max_abs_error(plane, original, 9 * 7) > 1.0);
    DIC_EXPECT(dic_dwt97_inverse_plane(plane, 9, 7, 2) == DIC_STATUS_OK);
    DIC_EXPECT(dic_test_max_abs_error(plane, original, 9 * 7) <= 1e-9);

    return 0;
}

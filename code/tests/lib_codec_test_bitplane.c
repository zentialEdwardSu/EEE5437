#include <string.h>

#include "codec/dic_bitplane.h"
#include "test_helpers.h"

int main(void)
{
    const int32_t coefficients[] = {
        0, 1, -1, 2, -3, 7, -8, 15,
        -16, 31, 64, -127, 255, -256, 1023, -2048
    };
    int32_t decoded[sizeof(coefficients) / sizeof(coefficients[0])];
    int32_t partial[sizeof(coefficients) / sizeof(coefficients[0])];
    dic_bitplane_stream stream = {0};
    size_t i;

    DIC_EXPECT(dic_bitplane_required_bits_i32(0) == 0);
    DIC_EXPECT(dic_bitplane_required_bits_i32(1) == 1);
    DIC_EXPECT(dic_bitplane_required_bits_i32(-1) == 1);
    DIC_EXPECT(dic_bitplane_required_bits_i32(255) == 8);

    DIC_EXPECT(dic_bitplane_encode_i32(coefficients, sizeof(coefficients) / sizeof(coefficients[0]), &stream) == DIC_STATUS_OK);
    DIC_EXPECT(stream.max_bitplanes == 12);
    DIC_EXPECT(stream.bit_count > sizeof(coefficients) / sizeof(coefficients[0]));

    DIC_EXPECT(dic_bitplane_decode_i32(&stream, 0, decoded) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(decoded, coefficients, sizeof(coefficients)) == 0);

    DIC_EXPECT(dic_bitplane_decode_i32(&stream, 4, partial) == DIC_STATUS_OK);
    for (i = 0; i < sizeof(coefficients) / sizeof(coefficients[0]); ++i)
    {
        int32_t magnitude = coefficients[i] < 0 ? -coefficients[i] : coefficients[i];
        int32_t partial_magnitude = partial[i] < 0 ? -partial[i] : partial[i];

        DIC_EXPECT(partial_magnitude <= magnitude);
        if (partial[i] != 0)
            DIC_EXPECT((partial[i] < 0) == (coefficients[i] < 0));
    }

    dic_bitplane_stream_free(&stream);
    return 0;
}

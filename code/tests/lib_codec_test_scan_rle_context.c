#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "codec/scan.h"
#include "test_helpers.h"

int main(void) {
    int32_t plane[32 * 32];
    int32_t decoded[32 * 32];
    codec_scan_bitplane* bitplanes = NULL;
    int bitplane_count = 0;
    int i;

    memset(plane, 0, sizeof(plane));
    plane[0] = 31;
    plane[31] = -17;
    plane[31 * 32] = 9;
    plane[31 * 32 + 31] = -5;

    DIC_EXPECT(codec_scan_encode_plane(plane, 32, 32, 3, &bitplanes,
                                       &bitplane_count) == DIC_STATUS_OK);
    DIC_EXPECT(bitplane_count == 5);
    for (i = 0; i < bitplane_count; ++i) {
        DIC_EXPECT(bitplanes[i].dominant_command_count <=
                   bitplanes[i].dominant_token_count);
        DIC_EXPECT(bitplanes[i].subordinate_mode ==
                       DIC_SCAN_REFINEMENT_RAW ||
                   bitplanes[i].subordinate_mode ==
                       DIC_SCAN_REFINEMENT_ARITHMETIC);
    }

    memset(decoded, 0, sizeof(decoded));
    DIC_EXPECT(codec_scan_decode_plane(bitplanes, bitplane_count,
                                       bitplane_count, 32, 32, 3,
                                       decoded) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(plane, decoded, sizeof(plane)) == 0);

    for (i = 0; i < bitplane_count; ++i)
        codec_scan_bitplane_free(bitplanes + i);
    free(bitplanes);

    /* Fine-scale significance creates long IZ sequences at coarse scales. */
    memset(plane, 0, sizeof(plane));
    for (i = 0; i < 32 * 32; ++i) {
        int x = i % 32;
        int y = i / 32;
        if (x >= 16 || y >= 16) plane[i] = 1;
    }
    bitplanes = NULL;
    bitplane_count = 0;
    DIC_EXPECT(codec_scan_encode_plane(plane, 32, 32, 3, &bitplanes,
                                       &bitplane_count) == DIC_STATUS_OK);
    DIC_EXPECT(bitplane_count == 1);
    DIC_EXPECT(bitplanes[0].dominant_command_count <
               bitplanes[0].dominant_token_count);
    DIC_EXPECT(bitplanes[0].run_length_bit_count > 0u);
    {
        size_t saved = bitplanes[0].run_length_bit_count;
        --bitplanes[0].run_length_bit_count;
        DIC_EXPECT(codec_scan_decode_plane(bitplanes, 1, 1, 32, 32, 3,
                                           decoded) != DIC_STATUS_OK);
        bitplanes[0].run_length_bit_count = saved;
    }
    codec_scan_bitplane_free(bitplanes);
    free(bitplanes);

    /* Predictable refinement uses arithmetic coding; truncation is rejected. */
    for (i = 0; i < 32 * 32; ++i) plane[i] = 31;
    bitplanes = NULL;
    bitplane_count = 0;
    DIC_EXPECT(codec_scan_encode_plane(plane, 32, 32, 3, &bitplanes,
                                       &bitplane_count) == DIC_STATUS_OK);
    DIC_EXPECT(bitplane_count == 5);
    DIC_EXPECT(bitplanes[1].subordinate_mode ==
               DIC_SCAN_REFINEMENT_ARITHMETIC);
    DIC_EXPECT(bitplanes[1].subordinate_bit_count <
               bitplanes[1].subordinate_symbol_count);
    {
        size_t saved_bits = bitplanes[1].subordinate_bit_count;
        size_t saved_bytes = bitplanes[1].subordinate_byte_count;
        --bitplanes[1].subordinate_bit_count;
        bitplanes[1].subordinate_byte_count =
            (bitplanes[1].subordinate_bit_count + 7u) / 8u;
        DIC_EXPECT(codec_scan_decode_plane(bitplanes, bitplane_count,
                                           bitplane_count, 32, 32, 3,
                                           decoded) != DIC_STATUS_OK);
        bitplanes[1].subordinate_bit_count = saved_bits;
        bitplanes[1].subordinate_byte_count = saved_bytes;
    }
    memset(decoded, 0, sizeof(decoded));
    DIC_EXPECT(codec_scan_decode_plane(bitplanes, bitplane_count,
                                       bitplane_count, 32, 32, 3,
                                       decoded) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(plane, decoded, sizeof(plane)) == 0);
    for (i = 0; i < bitplane_count; ++i)
        codec_scan_bitplane_free(bitplanes + i);
    free(bitplanes);

    memset(plane, 0, sizeof(plane));
    bitplanes = NULL;
    bitplane_count = -1;
    DIC_EXPECT(codec_scan_encode_plane(plane, 32, 32, 3, &bitplanes,
                                       &bitplane_count) == DIC_STATUS_OK);
    DIC_EXPECT(bitplane_count == 0 && bitplanes == NULL);
    return 0;
}

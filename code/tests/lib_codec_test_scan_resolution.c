#include <stdint.h>
#include <string.h>

#include "codec/scan.h"
#include "test_helpers.h"

int main(void)
{
    /* Test: encode + decode LL only (resolution 0) */
    {
        int32_t plane[8 * 8];
        codec_scan_bitplane *bps = NULL;
        int bp_count = 0;
        int32_t decoded[2 * 2];
        int i;

        /* Fill LL region (top-left 2x2 in 2-level DWT 8x8) with known values */
        memset(plane, 0, sizeof(plane));
        plane[0] = 50;
        plane[1] = -30;
        plane[8] = 0;
        plane[9] = 10;

        DIC_EXPECT(codec_scan_encode_subbands(
            plane, 8, 8, 2, 0, &bps, &bp_count) == DIC_STATUS_OK);
        DIC_EXPECT(bp_count > 0);

        /* Decode into 2x2 plane (8>>2=2) with max_resolution=0 */
        memset(decoded, 0, sizeof(decoded));
        DIC_EXPECT(codec_scan_decode_subbands(
            bps, bp_count, bp_count, 2, 2, 0, 0, decoded) == DIC_STATUS_OK);

        DIC_EXPECT(decoded[0] == 50);
        DIC_EXPECT(decoded[1] == -30);
        DIC_EXPECT(decoded[2] == 0);
        DIC_EXPECT(decoded[3] == 10);

        for (i = 0; i < bp_count; ++i)
            codec_scan_bitplane_free(bps + i);
        free(bps);
    }

    /* Test: all-zero resolution produces 0 bitplanes */
    {
        int32_t plane[16 * 16];
        codec_scan_bitplane *bps = NULL;
        int bp_count = -1;

        memset(plane, 0, sizeof(plane));
        DIC_EXPECT(codec_scan_encode_subbands(
            plane, 16, 16, 3, 2, &bps, &bp_count) == DIC_STATUS_OK);
        DIC_EXPECT(bp_count == 0);
        DIC_EXPECT(bps == NULL);
    }

    /* Test: resolution r=1 encodes HL/LH/HH at correct level */
    {
        int32_t plane[16 * 16];
        codec_scan_bitplane *bps = NULL;
        int bp_count = 0;
        int32_t decoded[4 * 4];
        int i;

        /* Set a value in HL at level 3 (levels=3, res=1 => level=3) */
        memset(plane, 0, sizeof(plane));
        /* LL at (0,0) 2x2. HL at level 3 is at (2,0) 2x2 */
        plane[0 * 16 + 2] = 100;

        DIC_EXPECT(codec_scan_encode_subbands(
            plane, 16, 16, 3, 1, &bps, &bp_count) == DIC_STATUS_OK);
        DIC_EXPECT(bp_count > 0);

        memset(decoded, 0, sizeof(decoded));
        DIC_EXPECT(codec_scan_decode_subbands(
            bps, bp_count, bp_count, 4, 4, 1, 1, decoded) == DIC_STATUS_OK);
        DIC_EXPECT(decoded[0 * 4 + 2] == 100);

        for (i = 0; i < bp_count; ++i)
            codec_scan_bitplane_free(bps + i);
        free(bps);
    }

    /* Test: progressive quality — fewer bitplanes than available */
    {
        int32_t plane[4 * 4];
        codec_scan_bitplane *bps = NULL;
        int bp_count = 0;
        int32_t decoded[1 * 1];
        int i;

        memset(plane, 0, sizeof(plane));
        plane[0] = 7;  /* 0b111 — 3 bitplanes */

        DIC_EXPECT(codec_scan_encode_subbands(
            plane, 4, 4, 2, 0, &bps, &bp_count) == DIC_STATUS_OK);
        DIC_EXPECT(bp_count == 3);

        /* Decode only 1 bitplane — midpoint reconstruction should give value near 4+2=6 */
        memset(decoded, 0, sizeof(decoded));
        DIC_EXPECT(codec_scan_decode_subbands(
            bps, 3, 1, 1, 1, 0, 0, decoded) == DIC_STATUS_OK);
        DIC_EXPECT(decoded[0] >= 4);

        for (i = 0; i < bp_count; ++i)
            codec_scan_bitplane_free(bps + i);
        free(bps);
    }

    return 0;
}

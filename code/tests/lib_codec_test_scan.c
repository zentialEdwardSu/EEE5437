#include <string.h>

#include "codec/scan.h"
#include "test_helpers.h"

int main(void) {
  int32_t plane[8 * 8];
  int32_t reconstructed[8 * 8];
  codec_scan_bitplane* bitplanes = NULL;
  int bp_count = 0;
  int i;

  memset(plane, 0, sizeof(plane));
  plane[0] = 18;
  plane[1] = -3;
  plane[4] = 7;
  plane[8] = -5;
  plane[16] = 11;
  plane[63] = 2;

  DIC_EXPECT(codec_scan_encode_plane(plane, 8, 8, 3, &bitplanes, &bp_count) ==
             DIC_STATUS_OK);
  DIC_EXPECT(bp_count > 0);

  /* Full decode */
  DIC_EXPECT(codec_scan_decode_plane(bitplanes, bp_count, bp_count, 8, 8, 3,
                                     reconstructed) == DIC_STATUS_OK);
  DIC_EXPECT(memcmp(plane, reconstructed, sizeof(plane)) == 0);

  /* Progressive decode: 1 bitplane */
  {
    int32_t partial[8 * 8];
    memset(partial, 0, sizeof(partial));
    DIC_EXPECT(codec_scan_decode_plane(bitplanes, bp_count, 1, 8, 8, 3,
                                       partial) == DIC_STATUS_OK);
  }

  for (i = 0; i < bp_count; ++i) codec_scan_bitplane_free(bitplanes + i);
  free(bitplanes);
  return 0;
}

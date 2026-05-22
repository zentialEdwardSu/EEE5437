#include <string.h>

#include "codec/dic_scan.h"
#include "test_helpers.h"

int main(void)
{
    int32_t plane[8 * 8];
    int32_t reconstructed[8 * 8];
    dic_scan_symbol_buffer symbols = {0};
    int i;
    int ezt_seen = 0;

    memset(plane, 0, sizeof(plane));
    plane[0] = 18;
    plane[1] = -3;
    plane[4] = 7;
    plane[8] = -5;
    plane[16] = 11;
    plane[63] = 2;

    DIC_EXPECT(dic_scan_encode_plane(plane, 8, 8, 3, &symbols) == DIC_STATUS_OK);
    DIC_EXPECT(symbols.count > 0u);
    for (i = 0; i < (int)symbols.count; ++i)
    {
        if (symbols.symbols[i].kind == DIC_SCAN_SYMBOL_EZT)
            ezt_seen = 1;
    }
    DIC_EXPECT(ezt_seen);

    DIC_EXPECT(dic_scan_decode_plane(symbols.symbols, symbols.count, 8, 8, 3, reconstructed) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(plane, reconstructed, sizeof(plane)) == 0);

    dic_scan_symbol_buffer_free(&symbols);
    return 0;
}

/**
 * @file rct.c
 * @brief Implements the JPEG 2000 T.800 Annex G Reversible Component Transform.
 */

#include "codec/rct.h"

static int32_t codec_rct_floor_div4(int32_t value) {
    if (value >= 0) return value / 4;
    return -(((-value) + 3) / 4);
}

dic_status codec_rct_forward(int32_t* r_plane, int32_t* g_plane,
                             int32_t* b_plane, size_t pixel_count) {
    size_t i;

    if (r_plane == NULL || g_plane == NULL || b_plane == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (i = 0; i < pixel_count; ++i) {
        int32_t r = r_plane[i];
        int32_t g = g_plane[i];
        int32_t b = b_plane[i];

        /* Y  = floor((R + 2G + B) / 4) */
        r_plane[i] = codec_rct_floor_div4(r + 2 * g + b);
        /* Cb = B - G */
        g_plane[i] = b - g;
        /* Cr = R - G */
        b_plane[i] = r - g;
    }

    return DIC_STATUS_OK;
}

dic_status codec_rct_inverse(int32_t* y_plane, int32_t* cb_plane,
                             int32_t* cr_plane, size_t pixel_count) {
    size_t i;

    if (y_plane == NULL || cb_plane == NULL || cr_plane == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (i = 0; i < pixel_count; ++i) {
        int32_t y = y_plane[i];
        int32_t cb = cb_plane[i];
        int32_t cr = cr_plane[i];

        /* G = Y - floor((Cr + Cb) / 4) */
        int32_t g = y - codec_rct_floor_div4(cr + cb);
        /* R = Cr + G */
        int32_t r = cr + g;
        /* B = Cb + G */
        int32_t b = cb + g;

        y_plane[i] = r;
        cb_plane[i] = g;
        cr_plane[i] = b;
    }

    return DIC_STATUS_OK;
}

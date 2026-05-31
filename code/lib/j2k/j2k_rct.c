/**
 * @file j2k_rct.c
 * @brief Implements the reversible multiple component transform from T.800 Annex G.
 *
 * The forward and inverse routines operate in-place on interleaved RGB int32 samples and
 * implement the reversible YDbDr equations used by JPEG 2000. The implementation assumes
 * three equally sampled components, matching the encoder path, and leaves component
 * registration/sub-sampling concerns to higher layers.
 *
 * References: j2k_image.c for transform use, j2k_codestream.c for COD MCT signalling,
 * and Annex J.15 for guidance on YCC codestream handling.
 */

#include "j2k/j2k_rct.h"
#include "j2k/j2k_debug.h"

static int32_t j2k_rct_floor_div4(int32_t value)
{
    j2k_DEBUG_ENTER();
    if (value >= 0)
        return value / 4;
    return -(((-value) + 3) / 4);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G.2.1, reversible component transform maps RGB to Y, Db, Dr. */
dic_status j2k_rct_forward(
    int32_t *samples,
    size_t pixel_count
)
{
    j2k_DEBUG_ENTER();
    size_t pixel;

    if (samples == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (pixel = 0u; pixel < pixel_count; ++pixel)
    {
        int32_t r = samples[pixel * 3u + 0u];
        int32_t g = samples[pixel * 3u + 1u];
        int32_t b = samples[pixel * 3u + 2u];
        int32_t y = j2k_rct_floor_div4(r + (g << 1) + b);
        int32_t db = b - g;
        int32_t dr = r - g;

        samples[pixel * 3u + 0u] = y;
        samples[pixel * 3u + 1u] = db;
        samples[pixel * 3u + 2u] = dr;
    }

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G.2.1, inverse RCT reconstructs RGB from Y, Db, Dr. */
dic_status j2k_rct_inverse(
    int32_t *samples,
    size_t pixel_count
)
{
    j2k_DEBUG_ENTER();
    size_t pixel;

    if (samples == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (pixel = 0u; pixel < pixel_count; ++pixel)
    {
        int32_t y = samples[pixel * 3u + 0u];
        int32_t db = samples[pixel * 3u + 1u];
        int32_t dr = samples[pixel * 3u + 2u];
        int32_t g = y - j2k_rct_floor_div4(db + dr);
        int32_t r = dr + g;
        int32_t b = db + g;

        samples[pixel * 3u + 0u] = r;
        samples[pixel * 3u + 1u] = g;
        samples[pixel * 3u + 2u] = b;
    }

    return DIC_STATUS_OK;
}

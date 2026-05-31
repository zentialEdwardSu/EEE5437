#pragma once

#include <stdint.h>

#include "codec/dic_subband.h"
#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Builds a coefficient-domain Maxshift map by tracing an image ROI through reversible 5-3 synthesis dependencies. */
dic_status j2k_roi_build_shift_map(
    int width,
    int height,
    int levels,
    const dic_rect_i32 *roi_rect,
    uint8_t **shift_map
);

/** Applies the Maxshift magnitude scaling to every coefficient selected by a coefficient-domain ROI map. */
dic_status j2k_roi_apply_shift_map(
    int32_t *plane,
    int width,
    int height,
    const uint8_t *shift_map,
    uint8_t shift
);

#ifdef __cplusplus
}
#endif

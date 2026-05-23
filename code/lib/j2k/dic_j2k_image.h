#pragma once

#include <stdint.h>

#include "codec/dic_subband.h"
#include "errors/errors.h"
#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

dic_status dic_j2k_write_image_codestream(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels
);

dic_status dic_j2k_write_image_codestream_tiled(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int tile_width,
    int tile_height,
    uint16_t layers
);

/** Encodes a raw J2K codestream with RGN Maxshift applied to coefficients covered by the rectangular ROI. */
dic_status dic_j2k_write_image_codestream_roi(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    const dic_rect_i32 *roi_rect,
    uint8_t roi_shift
);

dic_status dic_j2k_write_image_jp2(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels
);

dic_status dic_j2k_write_image_jp2_tiled(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int tile_width,
    int tile_height,
    uint16_t layers
);

/** Encodes a JP2 file with RGN Maxshift applied to coefficients covered by the rectangular ROI. */
dic_status dic_j2k_write_image_jp2_roi(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    const dic_rect_i32 *roi_rect,
    uint8_t roi_shift
);

/** Decodes the supported reversible JPEG 2000 codestream subset into an owned 8-bit image. */
dic_status dic_j2k_read_image_codestream(
    const char *path,
    dic_image_u8 *image
);

/**
 * Decodes the first max_layers quality layers from the supported reversible raw J2K subset.
 *
 * A max_layers value of zero decodes every layer signalled in COD. Nonzero values must be
 * less than or equal to the signalled layer count and reconstruct the image from the packet
 * prefix ending after that quality layer.
 */
dic_status dic_j2k_read_image_codestream_layers(
    const char *path,
    uint16_t max_layers,
    dic_image_u8 *image
);

/** Decodes a JP2 file by locating its contiguous codestream box and reconstructing an owned 8-bit image. */
dic_status dic_j2k_read_image_jp2(
    const char *path,
    dic_image_u8 *image
);

/**
 * Decodes the first max_layers quality layers from a JP2 file containing the supported J2K subset.
 *
 * A max_layers value of zero decodes every layer signalled in COD. Nonzero values must be
 * less than or equal to the signalled layer count and reconstruct the image from the packet
 * prefix ending after that quality layer.
 */
dic_status dic_j2k_read_image_jp2_layers(
    const char *path,
    uint16_t max_layers,
    dic_image_u8 *image
);

#ifdef __cplusplus
}
#endif

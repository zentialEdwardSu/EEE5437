#pragma once

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

dic_status dic_j2k_write_image_jp2(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels
);

#ifdef __cplusplus
}
#endif

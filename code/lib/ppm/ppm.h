#pragma once

#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

dic_status dic_ppm_read(const char *path, dic_image_u8 *image);
dic_status dic_ppm_write(const char *path, const dic_image_u8 *image);

#ifdef __cplusplus
}
#endif

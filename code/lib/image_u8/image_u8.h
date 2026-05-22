#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_image_u8
{
    int width;
    int height;
    int channels;
    uint8_t *data;
} dic_image_u8;

void dic_image_u8_init(dic_image_u8 *image);
void dic_image_u8_free(dic_image_u8 *image);

dic_status dic_image_u8_alloc(
    dic_image_u8 *image,
    int width,
    int height,
    int channels
);

size_t dic_image_u8_sample_count(int width, int height, int channels);

#ifdef __cplusplus
}
#endif

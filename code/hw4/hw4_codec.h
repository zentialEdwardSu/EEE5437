#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIC_HW4_MAGIC "DIC53"
#define DIC_HW4_VERSION 2u

typedef struct dic_hw4_image_u8
{
    int width;
    int height;
    int channels;
    uint8_t *data;
} dic_hw4_image_u8;

typedef struct dic_hw4_encoded_image
{
    uint32_t version;
    uint32_t flags;
    int width;
    int height;
    int channels;
    int levels;
    int quality;
    size_t coefficient_count;
    int32_t *coefficients;
} dic_hw4_encoded_image;

size_t dic_hw4_coefficient_count(int width, int height, int channels);

void dic_hw4_image_free(dic_hw4_image_u8 *image);
void dic_hw4_encoded_free(dic_hw4_encoded_image *encoded);

dic_status dic_hw4_validate_codec_params(
    int width,
    int height,
    int channels,
    int levels,
    int quality
);

dic_status dic_hw4_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int levels,
    int quality,
    dic_hw4_encoded_image *encoded
);

dic_status dic_hw4_decode_image(
    const dic_hw4_encoded_image *encoded,
    dic_hw4_image_u8 *decoded
);

#ifdef __cplusplus
}
#endif

#pragma once

#include "codec/dic_basic_codec.h"
#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIC_TILED_FILE_MAGIC "DICT"
#define DIC_TILED_FILE_VERSION 1u

typedef struct dic_tiled_tile
{
    int x;
    int y;
    int width;
    int height;
    dic_basic_encoded_image encoded;
} dic_tiled_tile;

typedef struct dic_tiled_encoded_image
{
    int width;
    int height;
    int channels;
    int requested_levels;
    int quant_step;
    int tile_width;
    int tile_height;
    int tile_count;
    dic_tiled_tile *tiles;
} dic_tiled_encoded_image;

void dic_tiled_encoded_init(dic_tiled_encoded_image *encoded);
void dic_tiled_encoded_free(dic_tiled_encoded_image *encoded);

dic_status dic_tiled_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int requested_levels,
    int quant_step,
    int tile_width,
    int tile_height,
    dic_tiled_encoded_image *encoded
);

dic_status dic_tiled_decode_image(
    const dic_tiled_encoded_image *encoded,
    dic_image_u8 *decoded
);

dic_status dic_tiled_write_file(
    const char *path,
    const dic_tiled_encoded_image *encoded
);

dic_status dic_tiled_read_file(
    const char *path,
    dic_tiled_encoded_image *encoded
);

int dic_tiled_effective_levels(int width, int height, int requested_levels);

#ifdef __cplusplus
}
#endif

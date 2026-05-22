#pragma once

#include <stddef.h>
#include <stdint.h>

#include "codec/dic_scan.h"
#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_basic_channel_stream
{
    dic_scan_symbol *symbols;
    size_t symbol_count;
} dic_basic_channel_stream;

typedef struct dic_basic_encoded_image
{
    int width;
    int height;
    int channels;
    int levels;
    int quant_step;
    dic_basic_channel_stream *channel_streams;
} dic_basic_encoded_image;

void dic_basic_encoded_init(dic_basic_encoded_image *encoded);
void dic_basic_encoded_free(dic_basic_encoded_image *encoded);

dic_status dic_basic_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int levels,
    int quant_step,
    dic_basic_encoded_image *encoded
);

dic_status dic_basic_decode_image(
    const dic_basic_encoded_image *encoded,
    dic_image_u8 *decoded
);

size_t dic_basic_symbol_count(const dic_basic_encoded_image *encoded);

#ifdef __cplusplus
}
#endif

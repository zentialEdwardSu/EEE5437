#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_bitplane_stream
{
    size_t coefficient_count;
    int max_bitplanes;
    size_t bit_count;
    unsigned char *bytes;
} dic_bitplane_stream;

void dic_bitplane_stream_init(dic_bitplane_stream *stream);
void dic_bitplane_stream_free(dic_bitplane_stream *stream);

dic_status dic_bitplane_encode_i32(
    const int32_t *coefficients,
    size_t coefficient_count,
    dic_bitplane_stream *stream
);

dic_status dic_bitplane_decode_i32(
    const dic_bitplane_stream *stream,
    int decoded_bitplanes,
    int32_t *coefficients
);

dic_status dic_bitplane_prefix_bit_count(
    const dic_bitplane_stream *stream,
    int decoded_bitplanes,
    size_t *bit_count
);

int dic_bitplane_required_bits_i32(int32_t value);

#ifdef __cplusplus
}
#endif

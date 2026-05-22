#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dic_scan_symbol_kind
{
    DIC_SCAN_SYMBOL_ZERO = 0,
    DIC_SCAN_SYMBOL_EZT = 1,
    DIC_SCAN_SYMBOL_NONZERO = 2
} dic_scan_symbol_kind;

typedef struct dic_scan_symbol
{
    unsigned char kind;
    unsigned char size;
    int32_t amplitude;
} dic_scan_symbol;

typedef struct dic_scan_symbol_buffer
{
    dic_scan_symbol *symbols;
    size_t count;
    size_t capacity;
} dic_scan_symbol_buffer;

void dic_scan_symbol_buffer_init(dic_scan_symbol_buffer *buffer);
void dic_scan_symbol_buffer_free(dic_scan_symbol_buffer *buffer);

dic_status dic_scan_encode_plane(
    const int32_t *plane,
    int width,
    int height,
    int levels,
    dic_scan_symbol_buffer *symbols
);

dic_status dic_scan_decode_plane(
    const dic_scan_symbol *symbols,
    size_t symbol_count,
    int width,
    int height,
    int levels,
    int32_t *plane
);

unsigned char dic_scan_amplitude_size(int32_t amplitude);

#ifdef __cplusplus
}
#endif

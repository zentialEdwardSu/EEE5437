/**
 * @file dic_scan.c
 * @brief Implements coefficient scanning and embedded zerotree markers.
 */

#include "codec/dic_scan.h"

#include <stdlib.h>
#include <string.h>

#include "codec/dic_subband.h"
#include "wavelet/dic_dwt53.h"

void dic_scan_symbol_buffer_init(dic_scan_symbol_buffer *buffer)
{
    if (buffer == NULL)
        return;

    buffer->symbols = NULL;
    buffer->count = 0u;
    buffer->capacity = 0u;
}

void dic_scan_symbol_buffer_free(dic_scan_symbol_buffer *buffer)
{
    if (buffer == NULL)
        return;

    free(buffer->symbols);
    dic_scan_symbol_buffer_init(buffer);
}

static dic_status dic_scan_symbol_buffer_reserve(
    dic_scan_symbol_buffer *buffer,
    size_t additional
)
{
    size_t required;
    size_t capacity;
    dic_scan_symbol *symbols;

    if (buffer == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (additional > SIZE_MAX - buffer->count)
        return DIC_STATUS_MEMORY_ERROR;

    required = buffer->count + additional;
    if (required <= buffer->capacity)
        return DIC_STATUS_OK;

    capacity = buffer->capacity == 0u ? 256u : buffer->capacity;
    while (capacity < required)
    {
        if (capacity > SIZE_MAX / 2u)
        {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }

    symbols = (dic_scan_symbol *)realloc(buffer->symbols, capacity * sizeof(symbols[0]));
    if (symbols == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    buffer->symbols = symbols;
    buffer->capacity = capacity;
    return DIC_STATUS_OK;
}

unsigned char dic_scan_amplitude_size(int32_t amplitude)
{
    uint32_t magnitude;
    unsigned char bits = 0u;

    if (amplitude == 0)
        return 0u;

    magnitude = amplitude < 0
        ? (uint32_t)(-(amplitude + 1)) + 1u
        : (uint32_t)amplitude;
    while (magnitude != 0u)
    {
        ++bits;
        magnitude >>= 1;
    }

    return bits;
}

static dic_status dic_scan_append_symbol(
    dic_scan_symbol_buffer *buffer,
    unsigned char kind,
    int32_t amplitude
)
{
    dic_status status = dic_scan_symbol_buffer_reserve(buffer, 1u);
    dic_scan_symbol *symbol;

    if (status != DIC_STATUS_OK)
        return status;

    symbol = buffer->symbols + buffer->count;
    symbol->kind = kind;
    symbol->amplitude = amplitude;
    symbol->size = kind == DIC_SCAN_SYMBOL_NONZERO
        ? dic_scan_amplitude_size(amplitude)
        : 0u;
    ++buffer->count;
    return DIC_STATUS_OK;
}

static size_t dic_scan_index(int width, int x, int y)
{
    return ((size_t)y * (size_t)width) + (size_t)x;
}

static int dic_scan_descendants_are_zero(
    const int32_t *plane,
    int width,
    int height,
    int level,
    dic_subband_orientation orientation,
    int local_x,
    int local_y
)
{
    dic_rect_i32 child_rect;
    int dx;
    int dy;

    if (level <= 1)
        return 1;
    if (dic_subband_rect(width, height, level, level - 1, orientation, &child_rect) != DIC_STATUS_OK)
        return 0;

    for (dy = 0; dy < 2; ++dy)
    {
        for (dx = 0; dx < 2; ++dx)
        {
            int child_x = (local_x * 2) + dx;
            int child_y = (local_y * 2) + dy;
            int image_x;
            int image_y;

            if (child_x >= child_rect.width || child_y >= child_rect.height)
                continue;

            image_x = child_rect.x + child_x;
            image_y = child_rect.y + child_y;
            if (plane[dic_scan_index(width, image_x, image_y)] != 0)
                return 0;
            if (!dic_scan_descendants_are_zero(
                    plane,
                    width,
                    height,
                    level - 1,
                    orientation,
                    child_x,
                    child_y))
            {
                return 0;
            }
        }
    }

    return 1;
}

static void dic_scan_mark_descendants(
    unsigned char *visited,
    int width,
    int height,
    int level,
    dic_subband_orientation orientation,
    int local_x,
    int local_y
)
{
    dic_rect_i32 child_rect;
    int dx;
    int dy;

    if (level <= 1)
        return;
    if (dic_subband_rect(width, height, level, level - 1, orientation, &child_rect) != DIC_STATUS_OK)
        return;

    for (dy = 0; dy < 2; ++dy)
    {
        for (dx = 0; dx < 2; ++dx)
        {
            int child_x = (local_x * 2) + dx;
            int child_y = (local_y * 2) + dy;
            int image_x;
            int image_y;

            if (child_x >= child_rect.width || child_y >= child_rect.height)
                continue;

            image_x = child_rect.x + child_x;
            image_y = child_rect.y + child_y;
            visited[dic_scan_index(width, image_x, image_y)] = 1u;
            dic_scan_mark_descendants(
                visited,
                width,
                height,
                level - 1,
                orientation,
                child_x,
                child_y
            );
        }
    }
}

static dic_status dic_scan_encode_ll(
    const int32_t *plane,
    int width,
    dic_rect_i32 ll_rect,
    dic_scan_symbol_buffer *symbols
)
{
    int y;

    for (y = 0; y < ll_rect.height; ++y)
    {
        int x;

        for (x = 0; x < ll_rect.width; ++x)
        {
            int32_t value = plane[dic_scan_index(width, ll_rect.x + x, ll_rect.y + y)];
            dic_status status = dic_scan_append_symbol(
                symbols,
                value == 0 ? DIC_SCAN_SYMBOL_ZERO : DIC_SCAN_SYMBOL_NONZERO,
                value
            );
            if (status != DIC_STATUS_OK)
                return status;
        }
    }

    return DIC_STATUS_OK;
}

static dic_status dic_scan_encode_high_band(
    const int32_t *plane,
    int width,
    int height,
    int level,
    dic_subband_orientation orientation,
    unsigned char *visited,
    dic_scan_symbol_buffer *symbols
)
{
    dic_rect_i32 rect;
    dic_status status;
    int y;

    status = dic_subband_rect(width, height, level, level, orientation, &rect);
    if (status != DIC_STATUS_OK)
        return status;

    for (y = 0; y < rect.height; ++y)
    {
        int x;

        for (x = 0; x < rect.width; ++x)
        {
            int image_x = rect.x + x;
            int image_y = rect.y + y;
            size_t index = dic_scan_index(width, image_x, image_y);
            int32_t value;

            if (visited[index])
                continue;

            value = plane[index];
            visited[index] = 1u;
            if (value == 0
                && level > 1
                && dic_scan_descendants_are_zero(
                    plane,
                    width,
                    height,
                    level,
                    orientation,
                    x,
                    y))
            {
                dic_scan_mark_descendants(visited, width, height, level, orientation, x, y);
                status = dic_scan_append_symbol(symbols, DIC_SCAN_SYMBOL_EZT, 0);
            }
            else
            {
                status = dic_scan_append_symbol(
                    symbols,
                    value == 0 ? DIC_SCAN_SYMBOL_ZERO : DIC_SCAN_SYMBOL_NONZERO,
                    value
                );
            }

            if (status != DIC_STATUS_OK)
                return status;
        }
    }

    return DIC_STATUS_OK;
}

dic_status dic_scan_encode_plane(
    const int32_t *plane,
    int width,
    int height,
    int levels,
    dic_scan_symbol_buffer *symbols
)
{
    dic_rect_i32 ll_rect;
    unsigned char *visited = NULL;
    dic_status status;
    int level;

    if (plane == NULL || symbols == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK)
        return status;

    dic_scan_symbol_buffer_free(symbols);
    status = dic_subband_lowest_ll_rect(width, height, levels, &ll_rect);
    if (status != DIC_STATUS_OK)
        return status;

    status = dic_scan_encode_ll(plane, width, ll_rect, symbols);
    if (status != DIC_STATUS_OK)
    {
        dic_scan_symbol_buffer_free(symbols);
        return status;
    }

    visited = (unsigned char *)calloc((size_t)width * (size_t)height, 1u);
    if (visited == NULL)
    {
        dic_scan_symbol_buffer_free(symbols);
        return DIC_STATUS_MEMORY_ERROR;
    }

    for (level = levels; level >= 1; --level)
    {
        status = dic_scan_encode_high_band(
            plane,
            width,
            height,
            level,
            DIC_SUBBAND_HL,
            visited,
            symbols
        );
        if (status == DIC_STATUS_OK)
            status = dic_scan_encode_high_band(
                plane,
                width,
                height,
                level,
                DIC_SUBBAND_LH,
                visited,
                symbols
            );
        if (status == DIC_STATUS_OK)
            status = dic_scan_encode_high_band(
                plane,
                width,
                height,
                level,
                DIC_SUBBAND_HH,
                visited,
                symbols
            );
        if (status != DIC_STATUS_OK)
            break;
    }

    free(visited);
    if (status != DIC_STATUS_OK)
        dic_scan_symbol_buffer_free(symbols);
    return status;
}

static dic_status dic_scan_next_symbol(
    const dic_scan_symbol *symbols,
    size_t symbol_count,
    size_t *offset,
    dic_scan_symbol *symbol
)
{
    if (symbols == NULL || offset == NULL || symbol == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (*offset >= symbol_count)
        return DIC_HW4_FORMAT_ERROR;

    *symbol = symbols[*offset];
    ++(*offset);
    if (symbol->kind != DIC_SCAN_SYMBOL_ZERO
        && symbol->kind != DIC_SCAN_SYMBOL_EZT
        && symbol->kind != DIC_SCAN_SYMBOL_NONZERO)
    {
        return DIC_HW4_FORMAT_ERROR;
    }
    if (symbol->kind == DIC_SCAN_SYMBOL_NONZERO
        && (symbol->amplitude == 0 || symbol->size != dic_scan_amplitude_size(symbol->amplitude)))
    {
        return DIC_HW4_FORMAT_ERROR;
    }
    if (symbol->kind != DIC_SCAN_SYMBOL_NONZERO && symbol->amplitude != 0)
        return DIC_HW4_FORMAT_ERROR;

    return DIC_STATUS_OK;
}

static dic_status dic_scan_decode_ll(
    const dic_scan_symbol *symbols,
    size_t symbol_count,
    size_t *offset,
    int width,
    dic_rect_i32 ll_rect,
    int32_t *plane
)
{
    int y;

    for (y = 0; y < ll_rect.height; ++y)
    {
        int x;

        for (x = 0; x < ll_rect.width; ++x)
        {
            dic_scan_symbol symbol;
            dic_status status = dic_scan_next_symbol(symbols, symbol_count, offset, &symbol);

            if (status != DIC_STATUS_OK)
                return status;
            if (symbol.kind == DIC_SCAN_SYMBOL_EZT)
                return DIC_HW4_FORMAT_ERROR;

            plane[dic_scan_index(width, ll_rect.x + x, ll_rect.y + y)] =
                symbol.kind == DIC_SCAN_SYMBOL_NONZERO ? symbol.amplitude : 0;
        }
    }

    return DIC_STATUS_OK;
}

static dic_status dic_scan_decode_high_band(
    const dic_scan_symbol *symbols,
    size_t symbol_count,
    size_t *offset,
    int width,
    int height,
    int level,
    dic_subband_orientation orientation,
    unsigned char *visited,
    int32_t *plane
)
{
    dic_rect_i32 rect;
    dic_status status;
    int y;

    status = dic_subband_rect(width, height, level, level, orientation, &rect);
    if (status != DIC_STATUS_OK)
        return status;

    for (y = 0; y < rect.height; ++y)
    {
        int x;

        for (x = 0; x < rect.width; ++x)
        {
            int image_x = rect.x + x;
            int image_y = rect.y + y;
            size_t index = dic_scan_index(width, image_x, image_y);
            dic_scan_symbol symbol;

            if (visited[index])
                continue;

            status = dic_scan_next_symbol(symbols, symbol_count, offset, &symbol);
            if (status != DIC_STATUS_OK)
                return status;

            visited[index] = 1u;
            if (symbol.kind == DIC_SCAN_SYMBOL_EZT)
            {
                plane[index] = 0;
                dic_scan_mark_descendants(visited, width, height, level, orientation, x, y);
            }
            else
            {
                plane[index] = symbol.kind == DIC_SCAN_SYMBOL_NONZERO ? symbol.amplitude : 0;
            }
        }
    }

    return DIC_STATUS_OK;
}

dic_status dic_scan_decode_plane(
    const dic_scan_symbol *symbols,
    size_t symbol_count,
    int width,
    int height,
    int levels,
    int32_t *plane
)
{
    dic_rect_i32 ll_rect;
    unsigned char *visited = NULL;
    size_t offset = 0u;
    dic_status status;
    int level;

    if (symbols == NULL || plane == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK)
        return status;

    memset(plane, 0, (size_t)width * (size_t)height * sizeof(plane[0]));
    status = dic_subband_lowest_ll_rect(width, height, levels, &ll_rect);
    if (status != DIC_STATUS_OK)
        return status;

    status = dic_scan_decode_ll(symbols, symbol_count, &offset, width, ll_rect, plane);
    if (status != DIC_STATUS_OK)
        return status;

    visited = (unsigned char *)calloc((size_t)width * (size_t)height, 1u);
    if (visited == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    for (level = levels; level >= 1; --level)
    {
        status = dic_scan_decode_high_band(
            symbols,
            symbol_count,
            &offset,
            width,
            height,
            level,
            DIC_SUBBAND_HL,
            visited,
            plane
        );
        if (status == DIC_STATUS_OK)
            status = dic_scan_decode_high_band(
                symbols,
                symbol_count,
                &offset,
                width,
                height,
                level,
                DIC_SUBBAND_LH,
                visited,
                plane
            );
        if (status == DIC_STATUS_OK)
            status = dic_scan_decode_high_band(
                symbols,
                symbol_count,
                &offset,
                width,
                height,
                level,
                DIC_SUBBAND_HH,
                visited,
                plane
            );
        if (status != DIC_STATUS_OK)
            break;
    }

    free(visited);
    if (status != DIC_STATUS_OK)
        return status;
    return offset == symbol_count ? DIC_STATUS_OK : DIC_HW4_FORMAT_ERROR;
}

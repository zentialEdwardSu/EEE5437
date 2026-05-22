#include "codec/dic_bitplane.h"

#include <stdlib.h>
#include <string.h>

void dic_bitplane_stream_init(dic_bitplane_stream *stream)
{
    if (stream == NULL)
        return;

    stream->coefficient_count = 0u;
    stream->max_bitplanes = 0;
    stream->bit_count = 0u;
    stream->bytes = NULL;
}

void dic_bitplane_stream_free(dic_bitplane_stream *stream)
{
    if (stream == NULL)
        return;

    free(stream->bytes);
    dic_bitplane_stream_init(stream);
}

static uint32_t dic_bitplane_magnitude_i32(int32_t value)
{
    if (value >= 0)
        return (uint32_t)value;
    return (uint32_t)(-(value + 1)) + 1u;
}

int dic_bitplane_required_bits_i32(int32_t value)
{
    uint32_t magnitude = dic_bitplane_magnitude_i32(value);
    int bits = 0;

    while (magnitude != 0u)
    {
        ++bits;
        magnitude >>= 1;
    }

    return bits;
}

static dic_status dic_bitplane_reserve_bits(dic_bitplane_stream *stream, size_t additional_bits)
{
    size_t required_bits;
    size_t required_bytes;
    size_t current_bytes;
    unsigned char *bytes;

    if (stream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (additional_bits > SIZE_MAX - stream->bit_count)
        return DIC_STATUS_MEMORY_ERROR;

    required_bits = stream->bit_count + additional_bits;
    required_bytes = (required_bits + 7u) / 8u;
    current_bytes = (stream->bit_count + 7u) / 8u;
    if (required_bytes <= current_bytes)
        return DIC_STATUS_OK;

    bytes = (unsigned char *)realloc(stream->bytes, required_bytes);
    if (bytes == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    memset(bytes + current_bytes, 0, required_bytes - current_bytes);
    stream->bytes = bytes;
    return DIC_STATUS_OK;
}

static dic_status dic_bitplane_append_bit(dic_bitplane_stream *stream, unsigned int bit)
{
    dic_status status = dic_bitplane_reserve_bits(stream, 1u);

    if (status != DIC_STATUS_OK)
        return status;

    if (bit != 0u)
    {
        stream->bytes[stream->bit_count / 8u] |=
            (unsigned char)(1u << (7u - (unsigned int)(stream->bit_count % 8u)));
    }
    ++stream->bit_count;
    return DIC_STATUS_OK;
}

static unsigned int dic_bitplane_read_bit(const dic_bitplane_stream *stream, size_t *offset)
{
    unsigned int bit;

    bit = (unsigned int)((stream->bytes[*offset / 8u] >> (7u - (unsigned int)(*offset % 8u))) & 1u);
    ++(*offset);
    return bit;
}

dic_status dic_bitplane_encode_i32(
    const int32_t *coefficients,
    size_t coefficient_count,
    dic_bitplane_stream *stream
)
{
    unsigned char *significant = NULL;
    int max_bitplanes = 0;
    int bitplane;
    size_t i;

    if ((coefficients == NULL && coefficient_count > 0u) || stream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_bitplane_stream_free(stream);
    stream->coefficient_count = coefficient_count;

    for (i = 0; i < coefficient_count; ++i)
    {
        int bits = dic_bitplane_required_bits_i32(coefficients[i]);
        if (bits > max_bitplanes)
            max_bitplanes = bits;
    }
    stream->max_bitplanes = max_bitplanes;

    if (max_bitplanes == 0 || coefficient_count == 0u)
        return DIC_STATUS_OK;

    significant = (unsigned char *)calloc(coefficient_count, sizeof(significant[0]));
    if (significant == NULL)
    {
        dic_bitplane_stream_free(stream);
        return DIC_STATUS_MEMORY_ERROR;
    }

    for (bitplane = max_bitplanes - 1; bitplane >= 0; --bitplane)
    {
        for (i = 0; i < coefficient_count; ++i)
        {
            uint32_t magnitude = dic_bitplane_magnitude_i32(coefficients[i]);
            unsigned int bit = (unsigned int)((magnitude >> bitplane) & 1u);
            dic_status status = dic_bitplane_append_bit(stream, bit);

            if (status != DIC_STATUS_OK)
            {
                free(significant);
                dic_bitplane_stream_free(stream);
                return status;
            }

            if (!significant[i] && bit != 0u)
            {
                status = dic_bitplane_append_bit(stream, coefficients[i] < 0 ? 1u : 0u);
                if (status != DIC_STATUS_OK)
                {
                    free(significant);
                    dic_bitplane_stream_free(stream);
                    return status;
                }
                significant[i] = 1u;
            }
        }
    }

    free(significant);
    return DIC_STATUS_OK;
}

dic_status dic_bitplane_decode_i32(
    const dic_bitplane_stream *stream,
    int decoded_bitplanes,
    int32_t *coefficients
)
{
    uint32_t *magnitudes = NULL;
    unsigned char *significant = NULL;
    unsigned char *negative = NULL;
    int available_bitplanes;
    int stop_bitplane;
    int bitplane;
    size_t offset = 0u;
    size_t i;

    if (stream == NULL || (coefficients == NULL && stream->coefficient_count > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;
    if (stream->max_bitplanes < 0)
        return DIC_HW4_FORMAT_ERROR;

    if (stream->coefficient_count > 0u)
        memset(coefficients, 0, stream->coefficient_count * sizeof(coefficients[0]));
    if (stream->coefficient_count == 0u || stream->max_bitplanes == 0)
        return stream->bit_count == 0u ? DIC_STATUS_OK : DIC_HW4_FORMAT_ERROR;
    if (stream->bytes == NULL)
        return DIC_HW4_FORMAT_ERROR;

    available_bitplanes = decoded_bitplanes <= 0
        ? stream->max_bitplanes
        : decoded_bitplanes;
    if (available_bitplanes > stream->max_bitplanes)
        available_bitplanes = stream->max_bitplanes;
    if (available_bitplanes <= 0)
        return DIC_STATUS_OK;

    magnitudes = (uint32_t *)calloc(stream->coefficient_count, sizeof(magnitudes[0]));
    significant = (unsigned char *)calloc(stream->coefficient_count, sizeof(significant[0]));
    negative = (unsigned char *)calloc(stream->coefficient_count, sizeof(negative[0]));
    if (magnitudes == NULL || significant == NULL || negative == NULL)
    {
        free(magnitudes);
        free(significant);
        free(negative);
        return DIC_STATUS_MEMORY_ERROR;
    }

    stop_bitplane = stream->max_bitplanes - available_bitplanes;
    for (bitplane = stream->max_bitplanes - 1; bitplane >= stop_bitplane; --bitplane)
    {
        for (i = 0; i < stream->coefficient_count; ++i)
        {
            unsigned int bit;

            if (offset >= stream->bit_count)
            {
                free(magnitudes);
                free(significant);
                free(negative);
                return DIC_HW4_FORMAT_ERROR;
            }

            bit = dic_bitplane_read_bit(stream, &offset);
            if (bit != 0u)
                magnitudes[i] |= 1u << bitplane;

            if (!significant[i] && bit != 0u)
            {
                if (offset >= stream->bit_count)
                {
                    free(magnitudes);
                    free(significant);
                    free(negative);
                    return DIC_HW4_FORMAT_ERROR;
                }

                negative[i] = (unsigned char)dic_bitplane_read_bit(stream, &offset);
                significant[i] = 1u;
            }
        }
    }

    if (available_bitplanes == stream->max_bitplanes && offset != stream->bit_count)
    {
        free(magnitudes);
        free(significant);
        free(negative);
        return DIC_HW4_FORMAT_ERROR;
    }

    for (i = 0; i < stream->coefficient_count; ++i)
    {
        if (magnitudes[i] == 0u)
            coefficients[i] = 0;
        else if (negative[i])
            coefficients[i] = -((int32_t)magnitudes[i]);
        else
            coefficients[i] = (int32_t)magnitudes[i];
    }

    free(magnitudes);
    free(significant);
    free(negative);
    return DIC_STATUS_OK;
}

dic_status dic_bitplane_prefix_bit_count(
    const dic_bitplane_stream *stream,
    int decoded_bitplanes,
    size_t *bit_count
)
{
    unsigned char *significant = NULL;
    int available_bitplanes;
    int stop_bitplane;
    int bitplane;
    size_t offset = 0u;
    size_t i;

    if (stream == NULL || bit_count == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (stream->max_bitplanes < 0)
        return DIC_HW4_FORMAT_ERROR;

    *bit_count = 0u;
    if (stream->coefficient_count == 0u || stream->max_bitplanes == 0)
        return stream->bit_count == 0u ? DIC_STATUS_OK : DIC_HW4_FORMAT_ERROR;
    if (stream->bytes == NULL)
        return DIC_HW4_FORMAT_ERROR;

    available_bitplanes = decoded_bitplanes <= 0
        ? stream->max_bitplanes
        : decoded_bitplanes;
    if (available_bitplanes > stream->max_bitplanes)
        available_bitplanes = stream->max_bitplanes;
    if (available_bitplanes <= 0)
        return DIC_STATUS_OK;

    significant = (unsigned char *)calloc(stream->coefficient_count, sizeof(significant[0]));
    if (significant == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    stop_bitplane = stream->max_bitplanes - available_bitplanes;
    for (bitplane = stream->max_bitplanes - 1; bitplane >= stop_bitplane; --bitplane)
    {
        for (i = 0; i < stream->coefficient_count; ++i)
        {
            unsigned int bit;

            if (offset >= stream->bit_count)
            {
                free(significant);
                return DIC_HW4_FORMAT_ERROR;
            }

            bit = dic_bitplane_read_bit(stream, &offset);
            if (!significant[i] && bit != 0u)
            {
                if (offset >= stream->bit_count)
                {
                    free(significant);
                    return DIC_HW4_FORMAT_ERROR;
                }

                (void)dic_bitplane_read_bit(stream, &offset);
                significant[i] = 1u;
            }
        }
    }

    *bit_count = offset;
    free(significant);
    return DIC_STATUS_OK;
}

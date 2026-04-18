#include "hw4/hw4_dic53.h"

#include "hw1/hw1_text_prob_analyzer.h"
#include "hw2/hw2_huffman.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    DIC_HW4_RLE_TOKEN_ZERO_RUN = 0,
    DIC_HW4_RLE_TOKEN_LITERAL = 1,
    DIC_HW4_HUFFMAN_SYMBOL_COUNT = 256
};

typedef struct dic_hw4_byte_buffer
{
    unsigned char *data;
    size_t size;
    size_t capacity;
} dic_hw4_byte_buffer;

static FILE *dic_hw4_open_file(const char *path, const char *mode)
{
    FILE *file = NULL;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, mode) != 0)
        return NULL;
    return file;
#else
    return fopen(path, mode);
#endif
}

static int dic_hw4_write_u32_le(FILE *file, uint32_t value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24) & 0xffu);
    return fwrite(bytes, sizeof(bytes), 1u, file) == 1u;
}

static int dic_hw4_read_u32_le(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];

    if (value == NULL)
        return 0;
    if (fread(bytes, sizeof(bytes), 1u, file) != 1u)
        return 0;

    *value = (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
    return 1;
}

static void dic_hw4_byte_buffer_init(dic_hw4_byte_buffer *buffer)
{
    if (buffer == NULL)
        return;

    buffer->data = NULL;
    buffer->size = 0u;
    buffer->capacity = 0u;
}

static void dic_hw4_byte_buffer_free(dic_hw4_byte_buffer *buffer)
{
    if (buffer == NULL)
        return;

    free(buffer->data);
    dic_hw4_byte_buffer_init(buffer);
}

static dic_status dic_hw4_byte_buffer_reserve(dic_hw4_byte_buffer *buffer, size_t additional)
{
    size_t required_capacity;
    size_t new_capacity;
    unsigned char *new_data = NULL;

    if (buffer == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (additional > SIZE_MAX - buffer->size)
        return DIC_STATUS_MEMORY_ERROR;

    required_capacity = buffer->size + additional;
    if (required_capacity <= buffer->capacity)
        return DIC_STATUS_OK;

    new_capacity = buffer->capacity > 0u ? buffer->capacity : 64u;
    while (new_capacity < required_capacity)
    {
        if (new_capacity > (SIZE_MAX / 2u))
        {
            new_capacity = required_capacity;
            break;
        }
        new_capacity *= 2u;
    }

    new_data = (unsigned char *)realloc(buffer->data, new_capacity);
    if (new_data == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    buffer->data = new_data;
    buffer->capacity = new_capacity;
    return DIC_STATUS_OK;
}

static dic_status dic_hw4_byte_buffer_append_byte(dic_hw4_byte_buffer *buffer, unsigned char value)
{
    dic_status status = dic_hw4_byte_buffer_reserve(buffer, 1u);

    if (status != DIC_STATUS_OK)
        return status;

    buffer->data[buffer->size] = value;
    ++buffer->size;
    return DIC_STATUS_OK;
}

static dic_status dic_hw4_byte_buffer_append_varuint(dic_hw4_byte_buffer *buffer, uint32_t value)
{
    dic_status status;

    do
    {
        unsigned char byte = (unsigned char)(value & 0x7fu);
        value >>= 7;
        if (value != 0u)
            byte |= 0x80u;

        status = dic_hw4_byte_buffer_append_byte(buffer, byte);
        if (status != DIC_STATUS_OK)
            return status;
    } while (value != 0u);

    return DIC_STATUS_OK;
}

static dic_status dic_hw4_read_varuint(
    const unsigned char *data,
    size_t data_size,
    size_t *offset,
    uint32_t *value
)
{
    uint32_t result = 0u;
    unsigned int shift = 0u;

    if (data == NULL || offset == NULL || value == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    while (*offset < data_size)
    {
        const unsigned char byte = data[*offset];
        ++(*offset);

        if (shift >= 32u && (byte & 0x7fu) != 0u)
            return DIC_HW4_FORMAT_ERROR;

        result |= (uint32_t)(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0u)
        {
            *value = result;
            return DIC_STATUS_OK;
        }

        shift += 7u;
        if (shift >= 35u)
            return DIC_HW4_FORMAT_ERROR;
    }

    return DIC_HW4_FORMAT_ERROR;
}

static uint32_t dic_hw4_zigzag_encode_i32(int32_t value)
{
    if (value >= 0)
        return (uint32_t)value << 1;
    return (((uint32_t)(-(value + 1))) << 1) | 1u;
}

static int32_t dic_hw4_zigzag_decode_i32(uint32_t value)
{
    if ((value & 1u) == 0u)
        return (int32_t)(value >> 1);
    return -((int32_t)(value >> 1) + 1);
}

/*
 * The quantized coefficient planes contain many zeros, especially in the
 * higher-frequency bands. A tiny zero-run/literal token stream is enough to
 * expose that redundancy before we hand the bytes to HW1/HW2 for entropy
 * coding.
 */
static dic_status dic_hw4_encode_coefficients_to_tokens(
    const int32_t *coefficients,
    size_t coefficient_count,
    dic_hw4_byte_buffer *tokens
)
{
    size_t index = 0u;

    if (coefficients == NULL || tokens == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_hw4_byte_buffer_init(tokens);

    while (index < coefficient_count)
    {
        dic_status status;

        if (coefficients[index] == 0)
        {
            size_t run_length = 1u;

            while (index + run_length < coefficient_count
                && coefficients[index + run_length] == 0
                && run_length < (size_t)UINT32_MAX)
            {
                ++run_length;
            }

            status = dic_hw4_byte_buffer_append_byte(tokens, (unsigned char)DIC_HW4_RLE_TOKEN_ZERO_RUN);
            if (status != DIC_STATUS_OK)
            {
                dic_hw4_byte_buffer_free(tokens);
                return status;
            }

            status = dic_hw4_byte_buffer_append_varuint(tokens, (uint32_t)run_length);
            if (status != DIC_STATUS_OK)
            {
                dic_hw4_byte_buffer_free(tokens);
                return status;
            }

            index += run_length;
            continue;
        }

        status = dic_hw4_byte_buffer_append_byte(tokens, (unsigned char)DIC_HW4_RLE_TOKEN_LITERAL);
        if (status != DIC_STATUS_OK)
        {
            dic_hw4_byte_buffer_free(tokens);
            return status;
        }

        status = dic_hw4_byte_buffer_append_varuint(
            tokens,
            dic_hw4_zigzag_encode_i32(coefficients[index])
        );
        if (status != DIC_STATUS_OK)
        {
            dic_hw4_byte_buffer_free(tokens);
            return status;
        }

        ++index;
    }

    return DIC_STATUS_OK;
}

static dic_status dic_hw4_decode_tokens_to_coefficients(
    const unsigned char *tokens,
    size_t token_count,
    int32_t *coefficients,
    size_t coefficient_count
)
{
    size_t token_offset = 0u;
    size_t coefficient_offset = 0u;

    if (tokens == NULL || coefficients == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    while (token_offset < token_count)
    {
        const unsigned char token = tokens[token_offset];
        ++token_offset;

        if (token == (unsigned char)DIC_HW4_RLE_TOKEN_ZERO_RUN)
        {
            uint32_t run_length_u32 = 0u;
            size_t run_length = 0u;
            dic_status status = dic_hw4_read_varuint(tokens, token_count, &token_offset, &run_length_u32);

            if (status != DIC_STATUS_OK)
                return status;

            run_length = (size_t)run_length_u32;
            if (run_length == 0u || run_length > coefficient_count - coefficient_offset)
                return DIC_HW4_FORMAT_ERROR;

            memset(
                coefficients + coefficient_offset,
                0,
                run_length * sizeof(coefficients[0])
            );
            coefficient_offset += run_length;
            continue;
        }

        if (token == (unsigned char)DIC_HW4_RLE_TOKEN_LITERAL)
        {
            uint32_t zigzag_value = 0u;
            dic_status status = dic_hw4_read_varuint(tokens, token_count, &token_offset, &zigzag_value);

            if (status != DIC_STATUS_OK)
                return status;
            if (coefficient_offset >= coefficient_count)
                return DIC_HW4_FORMAT_ERROR;

            coefficients[coefficient_offset] = dic_hw4_zigzag_decode_i32(zigzag_value);
            ++coefficient_offset;
            continue;
        }

        return DIC_HW4_FORMAT_ERROR;
    }

    if (coefficient_offset != coefficient_count)
        return DIC_HW4_FORMAT_ERROR;
    return DIC_STATUS_OK;
}

static unsigned int dic_hw4_map_byte_symbol(const void *element)
{
    return (unsigned int)(*(const unsigned char *)element);
}

static void dic_hw4_write_byte_symbol(void *element, unsigned int symbol)
{
    *(unsigned char *)element = (unsigned char)symbol;
}

static dic_status dic_hw4_build_huffman_bitstream(
    const unsigned char *tokens,
    size_t token_count,
    size_t counts[DIC_HW4_HUFFMAN_SYMBOL_COUNT],
    dic_hw2_huffman_bitstream *bitstream
)
{
    dic_hw1_text_analysis analysis;
    dic_hw2_huffman_tree tree;
    dic_status status;
    size_t symbol = 0u;

    if (tokens == NULL || counts == NULL || bitstream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_hw1_analyze_text(tokens, token_count, &analysis);
    if (status != DIC_STATUS_OK)
        return status;

    for (symbol = 0; symbol < DIC_HW4_HUFFMAN_SYMBOL_COUNT; ++symbol)
        counts[symbol] = analysis.counts[symbol];

    dic_hw2_huffman_tree_init(&tree);
    dic_hw2_huffman_bitstream_init(bitstream);

    /*
     * HW1 supplies the byte histogram of the token stream, and HW2 turns that
     * histogram into a canonical Huffman code and packed bitstream.
     */
    status = dic_hw2_huffman_build_from_counts(counts, DIC_HW4_HUFFMAN_SYMBOL_COUNT, &tree);
    if (status == DIC_STATUS_OK)
    {
        status = dic_hw2_huffman_encode_mapped(
            &tree,
            tokens,
            token_count,
            sizeof(tokens[0]),
            dic_hw4_map_byte_symbol,
            bitstream
        );
    }

    dic_hw2_huffman_tree_free(&tree);
    if (status != DIC_STATUS_OK)
        dic_hw2_huffman_bitstream_free(bitstream);
    return status;
}

static dic_status dic_hw4_rebuild_tokens_from_huffman(
    FILE *file,
    int32_t *coefficients,
    size_t coefficient_count
)
{
    size_t counts[DIC_HW4_HUFFMAN_SYMBOL_COUNT];
    size_t token_count = 0u;
    unsigned char *tokens = NULL;
    dic_hw2_huffman_tree tree;
    dic_hw2_huffman_bitstream bitstream;
    uint32_t bit_count_u32 = 0u;
    size_t byte_count = 0u;
    size_t symbol = 0u;
    dic_status status = DIC_STATUS_OK;

    if (file == NULL || coefficients == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (symbol = 0; symbol < DIC_HW4_HUFFMAN_SYMBOL_COUNT; ++symbol)
    {
        uint32_t count_u32 = 0u;

        if (!dic_hw4_read_u32_le(file, &count_u32))
            return DIC_HW4_FORMAT_ERROR;
        counts[symbol] = (size_t)count_u32;
        if (counts[symbol] > SIZE_MAX - token_count)
            return DIC_HW4_FORMAT_ERROR;
        token_count += counts[symbol];
    }

    if (!dic_hw4_read_u32_le(file, &bit_count_u32))
        return DIC_HW4_FORMAT_ERROR;

    byte_count = ((size_t)bit_count_u32 + 7u) / 8u;
    dic_hw2_huffman_tree_init(&tree);
    dic_hw2_huffman_bitstream_init(&bitstream);

    if (byte_count > 0u)
    {
        bitstream.bytes = (unsigned char *)malloc(byte_count);
        if (bitstream.bytes == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        if (fread(bitstream.bytes, 1u, byte_count, file) != byte_count)
        {
            dic_hw2_huffman_bitstream_free(&bitstream);
            return DIC_HW4_FORMAT_ERROR;
        }
    }

    bitstream.byte_count = byte_count;
    bitstream.bit_count = (size_t)bit_count_u32;

    tokens = token_count > 0u ? (unsigned char *)malloc(token_count) : NULL;
    if (token_count > 0u && tokens == NULL)
    {
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_STATUS_MEMORY_ERROR;
    }

    status = dic_hw2_huffman_build_from_counts(counts, DIC_HW4_HUFFMAN_SYMBOL_COUNT, &tree);
    if (status == DIC_STATUS_OK)
    {
        status = dic_hw2_huffman_decode_mapped(
            &tree,
            &bitstream,
            token_count,
            tokens,
            sizeof(tokens[0]),
            dic_hw4_write_byte_symbol
        );
    }

    if (status == DIC_STATUS_OK)
        status = dic_hw4_decode_tokens_to_coefficients(tokens, token_count, coefficients, coefficient_count);

    free(tokens);
    dic_hw2_huffman_bitstream_free(&bitstream);
    dic_hw2_huffman_tree_free(&tree);
    return status;
}

dic_status dic_hw4_write_encoded_file(
    const char *path,
    const dic_hw4_encoded_image *encoded
)
{
    FILE *file = NULL;
    dic_hw4_byte_buffer tokens;
    dic_hw2_huffman_bitstream bitstream;
    size_t counts[DIC_HW4_HUFFMAN_SYMBOL_COUNT];
    size_t symbol = 0u;
    dic_status status;

    if (path == NULL || encoded == NULL || encoded->coefficients == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_hw4_byte_buffer_init(&tokens);
    dic_hw2_huffman_bitstream_init(&bitstream);

    status = dic_hw4_encode_coefficients_to_tokens(
        encoded->coefficients,
        encoded->coefficient_count,
        &tokens
    );
    if (status != DIC_STATUS_OK)
        return status;

    status = dic_hw4_build_huffman_bitstream(tokens.data, tokens.size, counts, &bitstream);
    if (status != DIC_STATUS_OK)
    {
        dic_hw4_byte_buffer_free(&tokens);
        return status;
    }

    if (bitstream.bit_count > (size_t)UINT32_MAX)
    {
        dic_hw4_byte_buffer_free(&tokens);
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_HW4_FORMAT_ERROR;
    }

    file = dic_hw4_open_file(path, "wb");
    if (file == NULL)
    {
        dic_hw4_byte_buffer_free(&tokens);
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_STATUS_IO_ERROR;
    }

    if (fwrite(DIC_HW4_MAGIC, 5u, 1u, file) != 1u
        || !dic_hw4_write_u32_le(file, encoded->version)
        || !dic_hw4_write_u32_le(file, (uint32_t)encoded->width)
        || !dic_hw4_write_u32_le(file, (uint32_t)encoded->height)
        || !dic_hw4_write_u32_le(file, (uint32_t)encoded->channels)
        || !dic_hw4_write_u32_le(file, (uint32_t)encoded->levels)
        || !dic_hw4_write_u32_le(file, (uint32_t)encoded->quality)
        || !dic_hw4_write_u32_le(file, encoded->flags))
    {
        fclose(file);
        dic_hw4_byte_buffer_free(&tokens);
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_STATUS_IO_ERROR;
    }

    for (symbol = 0u; symbol < DIC_HW4_HUFFMAN_SYMBOL_COUNT; ++symbol)
    {
        if (counts[symbol] > (size_t)UINT32_MAX
            || !dic_hw4_write_u32_le(file, (uint32_t)counts[symbol]))
        {
            fclose(file);
            dic_hw4_byte_buffer_free(&tokens);
            dic_hw2_huffman_bitstream_free(&bitstream);
            return DIC_STATUS_IO_ERROR;
        }
    }

    if (!dic_hw4_write_u32_le(file, (uint32_t)bitstream.bit_count)
        || (bitstream.byte_count > 0u
            && fwrite(bitstream.bytes, 1u, bitstream.byte_count, file) != bitstream.byte_count))
    {
        fclose(file);
        dic_hw4_byte_buffer_free(&tokens);
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_STATUS_IO_ERROR;
    }

    if (fclose(file) != 0)
    {
        dic_hw4_byte_buffer_free(&tokens);
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_STATUS_IO_ERROR;
    }

    dic_hw4_byte_buffer_free(&tokens);
    dic_hw2_huffman_bitstream_free(&bitstream);
    return DIC_STATUS_OK;
}

dic_status dic_hw4_read_encoded_file(
    const char *path,
    dic_hw4_encoded_image *encoded
)
{
    FILE *file = NULL;
    char magic[5];
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t levels;
    uint32_t quality;
    uint32_t flags;
    size_t coefficient_count;
    dic_status status;

    if (path == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_hw4_encoded_free(encoded);

    file = dic_hw4_open_file(path, "rb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (fread(magic, sizeof(magic), 1u, file) != 1u
        || memcmp(magic, DIC_HW4_MAGIC, sizeof(magic)) != 0
        || !dic_hw4_read_u32_le(file, &version)
        || !dic_hw4_read_u32_le(file, &width)
        || !dic_hw4_read_u32_le(file, &height)
        || !dic_hw4_read_u32_le(file, &channels)
        || !dic_hw4_read_u32_le(file, &levels)
        || !dic_hw4_read_u32_le(file, &quality)
        || !dic_hw4_read_u32_le(file, &flags))
    {
        fclose(file);
        return DIC_HW4_FORMAT_ERROR;
    }

    if (version != DIC_HW4_VERSION)
    {
        fclose(file);
        return DIC_HW4_FORMAT_ERROR;
    }

    status = dic_hw4_validate_codec_params(
        (int)width,
        (int)height,
        (int)channels,
        (int)levels,
        (int)quality
    );
    if (status != DIC_STATUS_OK)
    {
        fclose(file);
        return DIC_HW4_FORMAT_ERROR;
    }

    coefficient_count = dic_hw4_coefficient_count((int)width, (int)height, (int)channels);
    encoded->coefficients = (int32_t *)malloc(coefficient_count * sizeof(int32_t));
    if (encoded->coefficients == NULL)
    {
        fclose(file);
        return DIC_STATUS_MEMORY_ERROR;
    }

    status = dic_hw4_rebuild_tokens_from_huffman(file, encoded->coefficients, coefficient_count);
    if (status != DIC_STATUS_OK)
    {
        dic_hw4_encoded_free(encoded);
        fclose(file);
        return status == DIC_HW2_HUFFMAN_MALFORMED_BITSTREAM ? DIC_HW4_FORMAT_ERROR : status;
    }

    if (fgetc(file) != EOF)
    {
        dic_hw4_encoded_free(encoded);
        fclose(file);
        return DIC_HW4_FORMAT_ERROR;
    }

    fclose(file);
    encoded->version = version;
    encoded->flags = flags;
    encoded->width = (int)width;
    encoded->height = (int)height;
    encoded->channels = (int)channels;
    encoded->levels = (int)levels;
    encoded->quality = (int)quality;
    encoded->coefficient_count = coefficient_count;
    return DIC_STATUS_OK;
}

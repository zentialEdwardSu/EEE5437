/**
 * @file dic_basic_file.c
 * @brief Implements DICW serialization with Huffman-coded scan-symbol tokens.
 */

#include "codec/dic_basic_file.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw2/hw2_huffman.h"

enum
{
    DIC_BASIC_TOKEN_ZERO = 0,
    DIC_BASIC_TOKEN_EZT = 1,
    DIC_BASIC_TOKEN_SIZE_BASE = 2,
    DIC_BASIC_TOKEN_COUNT = 34
};

static FILE *dic_basic_open_file(const char *path, const char *mode)
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

static int dic_basic_write_u32_le(FILE *file, uint32_t value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24) & 0xffu);
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes);
}

static int dic_basic_write_i32_le(FILE *file, int32_t value)
{
    return dic_basic_write_u32_le(file, (uint32_t)value);
}

static int dic_basic_read_u32_le(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];

    if (file == NULL || value == NULL)
        return 0;
    if (fread(bytes, 1u, sizeof(bytes), file) != sizeof(bytes))
        return 0;

    *value = (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
    return 1;
}

static int dic_basic_read_i32_le(FILE *file, int32_t *value)
{
    uint32_t raw;

    if (!dic_basic_read_u32_le(file, &raw))
        return 0;
    *value = (int32_t)raw;
    return 1;
}

static unsigned int dic_basic_map_token(const void *element)
{
    return (unsigned int)(*(const unsigned char *)element);
}

static void dic_basic_write_token(void *element, unsigned int symbol)
{
    *(unsigned char *)element = (unsigned char)symbol;
}

static dic_status dic_basic_symbols_to_tokens(
    const dic_scan_symbol *symbols,
    size_t symbol_count,
    unsigned char **tokens_out,
    size_t counts[DIC_BASIC_TOKEN_COUNT],
    size_t *amplitude_count_out
)
{
    unsigned char *tokens = NULL;
    size_t amplitude_count = 0u;
    size_t i;

    if (symbols == NULL || tokens_out == NULL || counts == NULL || amplitude_count_out == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    tokens = (unsigned char *)malloc(symbol_count);
    if (tokens == NULL && symbol_count > 0u)
        return DIC_STATUS_MEMORY_ERROR;

    memset(counts, 0, DIC_BASIC_TOKEN_COUNT * sizeof(counts[0]));
    for (i = 0; i < symbol_count; ++i)
    {
        unsigned char token;

        if (symbols[i].kind == DIC_SCAN_SYMBOL_ZERO)
        {
            if (symbols[i].amplitude != 0 || symbols[i].size != 0u)
            {
                free(tokens);
                return DIC_HW4_FORMAT_ERROR;
            }
            token = (unsigned char)DIC_BASIC_TOKEN_ZERO;
        }
        else if (symbols[i].kind == DIC_SCAN_SYMBOL_EZT)
        {
            if (symbols[i].amplitude != 0 || symbols[i].size != 0u)
            {
                free(tokens);
                return DIC_HW4_FORMAT_ERROR;
            }
            token = (unsigned char)DIC_BASIC_TOKEN_EZT;
        }
        else if (symbols[i].kind == DIC_SCAN_SYMBOL_NONZERO)
        {
            if (symbols[i].amplitude == 0
                || symbols[i].size == 0u
                || symbols[i].size > 32u
                || symbols[i].size != dic_scan_amplitude_size(symbols[i].amplitude))
            {
                free(tokens);
                return DIC_HW4_FORMAT_ERROR;
            }
            token = (unsigned char)(DIC_BASIC_TOKEN_SIZE_BASE + symbols[i].size - 1u);
            ++amplitude_count;
        }
        else
        {
            free(tokens);
            return DIC_HW4_FORMAT_ERROR;
        }

        tokens[i] = token;
        ++counts[token];
    }

    *tokens_out = tokens;
    *amplitude_count_out = amplitude_count;
    return DIC_STATUS_OK;
}

static dic_status dic_basic_write_channel(
    FILE *file,
    const dic_basic_channel_stream *stream
)
{
    size_t counts[DIC_BASIC_TOKEN_COUNT];
    unsigned char *tokens = NULL;
    size_t amplitude_count = 0u;
    dic_hw2_huffman_tree tree;
    dic_hw2_huffman_bitstream bitstream;
    dic_status status;
    size_t i;

    if (file == NULL || stream == NULL || stream->symbols == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (stream->symbol_count > (size_t)UINT32_MAX)
        return DIC_HW4_FORMAT_ERROR;

    dic_hw2_huffman_tree_init(&tree);
    dic_hw2_huffman_bitstream_init(&bitstream);

    status = dic_basic_symbols_to_tokens(
        stream->symbols,
        stream->symbol_count,
        &tokens,
        counts,
        &amplitude_count
    );
    if (status != DIC_STATUS_OK)
        return status;
    if (amplitude_count > (size_t)UINT32_MAX)
    {
        free(tokens);
        return DIC_HW4_FORMAT_ERROR;
    }

    status = dic_hw2_huffman_build_from_counts(counts, DIC_BASIC_TOKEN_COUNT, &tree);
    if (status == DIC_STATUS_OK)
    {
        status = dic_hw2_huffman_encode_mapped(
            &tree,
            tokens,
            stream->symbol_count,
            sizeof(tokens[0]),
            dic_basic_map_token,
            &bitstream
        );
    }
    free(tokens);

    if (status != DIC_STATUS_OK)
    {
        dic_hw2_huffman_tree_free(&tree);
        return status;
    }
    if (bitstream.bit_count > (size_t)UINT32_MAX)
    {
        dic_hw2_huffman_tree_free(&tree);
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_HW4_FORMAT_ERROR;
    }

    if (!dic_basic_write_u32_le(file, (uint32_t)stream->symbol_count)
        || !dic_basic_write_u32_le(file, (uint32_t)amplitude_count))
    {
        dic_hw2_huffman_tree_free(&tree);
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_STATUS_IO_ERROR;
    }

    for (i = 0; i < DIC_BASIC_TOKEN_COUNT; ++i)
    {
        if (counts[i] > (size_t)UINT32_MAX
            || !dic_basic_write_u32_le(file, (uint32_t)counts[i]))
        {
            dic_hw2_huffman_tree_free(&tree);
            dic_hw2_huffman_bitstream_free(&bitstream);
            return DIC_STATUS_IO_ERROR;
        }
    }

    if (!dic_basic_write_u32_le(file, (uint32_t)bitstream.bit_count)
        || (bitstream.byte_count > 0u
            && fwrite(bitstream.bytes, 1u, bitstream.byte_count, file) != bitstream.byte_count))
    {
        dic_hw2_huffman_tree_free(&tree);
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_STATUS_IO_ERROR;
    }

    for (i = 0; i < stream->symbol_count; ++i)
    {
        if (stream->symbols[i].kind != DIC_SCAN_SYMBOL_NONZERO)
            continue;
        if (!dic_basic_write_i32_le(file, stream->symbols[i].amplitude))
        {
            dic_hw2_huffman_tree_free(&tree);
            dic_hw2_huffman_bitstream_free(&bitstream);
            return DIC_STATUS_IO_ERROR;
        }
    }

    dic_hw2_huffman_tree_free(&tree);
    dic_hw2_huffman_bitstream_free(&bitstream);
    return DIC_STATUS_OK;
}

dic_status dic_basic_write_file(
    const char *path,
    const dic_basic_encoded_image *encoded
)
{
    FILE *file = NULL;
    dic_status status;

    if (path == NULL || encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = dic_basic_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    status = dic_basic_write_stream(file, encoded);
    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

dic_status dic_basic_write_stream(
    FILE *file,
    const dic_basic_encoded_image *encoded
)
{
    dic_status status = DIC_STATUS_OK;
    int channel;

    if (file == NULL || encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (encoded->width <= 0 || encoded->height <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;
    if (encoded->channels != 1 && encoded->channels != 3)
        return DIC_HW4_INVALID_CHANNELS;
    if (encoded->levels <= 0 || encoded->quant_step <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    if (fwrite(DIC_BASIC_FILE_MAGIC, 1u, 4u, file) != 4u
        || !dic_basic_write_u32_le(file, DIC_BASIC_FILE_VERSION)
        || !dic_basic_write_u32_le(file, (uint32_t)encoded->width)
        || !dic_basic_write_u32_le(file, (uint32_t)encoded->height)
        || !dic_basic_write_u32_le(file, (uint32_t)encoded->channels)
        || !dic_basic_write_u32_le(file, (uint32_t)encoded->levels)
        || !dic_basic_write_u32_le(file, (uint32_t)encoded->quant_step))
    {
        return DIC_STATUS_IO_ERROR;
    }

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        status = dic_basic_write_channel(file, encoded->channel_streams + channel);
        if (status != DIC_STATUS_OK)
            break;
    }

    return status;
}

static dic_status dic_basic_rebuild_symbols(
    const unsigned char *tokens,
    size_t token_count,
    const int32_t *amplitudes,
    size_t amplitude_count,
    dic_scan_symbol **symbols_out
)
{
    dic_scan_symbol *symbols = NULL;
    size_t amplitude_offset = 0u;
    size_t i;

    if (tokens == NULL || symbols_out == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (amplitudes == NULL && amplitude_count > 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    symbols = (dic_scan_symbol *)calloc(token_count, sizeof(symbols[0]));
    if (symbols == NULL && token_count > 0u)
        return DIC_STATUS_MEMORY_ERROR;

    for (i = 0; i < token_count; ++i)
    {
        unsigned char token = tokens[i];

        if (token == DIC_BASIC_TOKEN_ZERO)
        {
            symbols[i].kind = DIC_SCAN_SYMBOL_ZERO;
            continue;
        }

        if (token == DIC_BASIC_TOKEN_EZT)
        {
            symbols[i].kind = DIC_SCAN_SYMBOL_EZT;
            continue;
        }

        if (token >= DIC_BASIC_TOKEN_SIZE_BASE && token < DIC_BASIC_TOKEN_COUNT)
        {
            int32_t amplitude;

            if (amplitude_offset >= amplitude_count)
            {
                free(symbols);
                return DIC_HW4_FORMAT_ERROR;
            }

            amplitude = amplitudes[amplitude_offset];
            ++amplitude_offset;
            symbols[i].kind = DIC_SCAN_SYMBOL_NONZERO;
            symbols[i].size = (unsigned char)(token - DIC_BASIC_TOKEN_SIZE_BASE + 1u);
            symbols[i].amplitude = amplitude;
            if (amplitude == 0 || symbols[i].size != dic_scan_amplitude_size(amplitude))
            {
                free(symbols);
                return DIC_HW4_FORMAT_ERROR;
            }
            continue;
        }

        free(symbols);
        return DIC_HW4_FORMAT_ERROR;
    }

    if (amplitude_offset != amplitude_count)
    {
        free(symbols);
        return DIC_HW4_FORMAT_ERROR;
    }

    *symbols_out = symbols;
    return DIC_STATUS_OK;
}

static dic_status dic_basic_read_channel(
    FILE *file,
    dic_basic_channel_stream *stream
)
{
    uint32_t symbol_count_u32;
    uint32_t amplitude_count_u32;
    uint32_t bit_count_u32;
    size_t counts[DIC_BASIC_TOKEN_COUNT];
    size_t expected_symbol_count = 0u;
    size_t symbol_count;
    size_t amplitude_count;
    size_t bit_byte_count;
    unsigned char *tokens = NULL;
    int32_t *amplitudes = NULL;
    dic_hw2_huffman_tree tree;
    dic_hw2_huffman_bitstream bitstream;
    dic_status status = DIC_STATUS_OK;
    size_t i;

    if (file == NULL || stream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    if (!dic_basic_read_u32_le(file, &symbol_count_u32)
        || !dic_basic_read_u32_le(file, &amplitude_count_u32))
    {
        return DIC_HW4_FORMAT_ERROR;
    }

    symbol_count = (size_t)symbol_count_u32;
    amplitude_count = (size_t)amplitude_count_u32;

    for (i = 0; i < DIC_BASIC_TOKEN_COUNT; ++i)
    {
        uint32_t count_u32;

        if (!dic_basic_read_u32_le(file, &count_u32))
            return DIC_HW4_FORMAT_ERROR;
        counts[i] = (size_t)count_u32;
        expected_symbol_count += counts[i];
    }

    if (expected_symbol_count != symbol_count)
        return DIC_HW4_FORMAT_ERROR;

    if (!dic_basic_read_u32_le(file, &bit_count_u32))
        return DIC_HW4_FORMAT_ERROR;

    bit_byte_count = ((size_t)bit_count_u32 + 7u) / 8u;
    dic_hw2_huffman_tree_init(&tree);
    dic_hw2_huffman_bitstream_init(&bitstream);

    if (bit_byte_count > 0u)
    {
        bitstream.bytes = (unsigned char *)malloc(bit_byte_count);
        if (bitstream.bytes == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        if (fread(bitstream.bytes, 1u, bit_byte_count, file) != bit_byte_count)
        {
            dic_hw2_huffman_bitstream_free(&bitstream);
            return DIC_HW4_FORMAT_ERROR;
        }
    }
    bitstream.byte_count = bit_byte_count;
    bitstream.bit_count = (size_t)bit_count_u32;

    tokens = (unsigned char *)malloc(symbol_count);
    if (tokens == NULL && symbol_count > 0u)
    {
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_STATUS_MEMORY_ERROR;
    }

    status = dic_hw2_huffman_build_from_counts(counts, DIC_BASIC_TOKEN_COUNT, &tree);
    if (status == DIC_STATUS_OK)
    {
        status = dic_hw2_huffman_decode_mapped(
            &tree,
            &bitstream,
            symbol_count,
            tokens,
            sizeof(tokens[0]),
            dic_basic_write_token
        );
    }

    dic_hw2_huffman_tree_free(&tree);
    dic_hw2_huffman_bitstream_free(&bitstream);
    if (status != DIC_STATUS_OK)
    {
        free(tokens);
        return status == DIC_HW2_HUFFMAN_MALFORMED_BITSTREAM ? DIC_HW4_FORMAT_ERROR : status;
    }

    amplitudes = amplitude_count > 0u
        ? (int32_t *)malloc(amplitude_count * sizeof(amplitudes[0]))
        : NULL;
    if (amplitudes == NULL && amplitude_count > 0u)
    {
        free(tokens);
        return DIC_STATUS_MEMORY_ERROR;
    }

    for (i = 0; i < amplitude_count; ++i)
    {
        if (!dic_basic_read_i32_le(file, amplitudes + i))
        {
            free(tokens);
            free(amplitudes);
            return DIC_HW4_FORMAT_ERROR;
        }
    }

    status = dic_basic_rebuild_symbols(
        tokens,
        symbol_count,
        amplitudes,
        amplitude_count,
        &stream->symbols
    );
    if (status == DIC_STATUS_OK)
        stream->symbol_count = symbol_count;

    free(tokens);
    free(amplitudes);
    return status;
}

dic_status dic_basic_read_file(
    const char *path,
    dic_basic_encoded_image *encoded
)
{
    FILE *file = NULL;
    dic_status status;

    if (path == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = dic_basic_open_file(path, "rb");
    if (file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;

    status = dic_basic_read_stream(file, encoded);
    if (status == DIC_STATUS_OK && fgetc(file) != EOF)
        status = DIC_HW4_FORMAT_ERROR;

    fclose(file);
    if (status != DIC_STATUS_OK)
        dic_basic_encoded_free(encoded);
    return status;
}

dic_status dic_basic_read_stream(
    FILE *file,
    dic_basic_encoded_image *encoded
)
{
    char magic[4];
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t levels;
    uint32_t quant_step;
    dic_status status = DIC_STATUS_OK;
    int channel;

    if (file == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_basic_encoded_free(encoded);

    if (fread(magic, 1u, sizeof(magic), file) != sizeof(magic)
        || memcmp(magic, DIC_BASIC_FILE_MAGIC, sizeof(magic)) != 0
        || !dic_basic_read_u32_le(file, &version)
        || !dic_basic_read_u32_le(file, &width)
        || !dic_basic_read_u32_le(file, &height)
        || !dic_basic_read_u32_le(file, &channels)
        || !dic_basic_read_u32_le(file, &levels)
        || !dic_basic_read_u32_le(file, &quant_step))
    {
        return DIC_HW4_FORMAT_ERROR;
    }

    if (version != DIC_BASIC_FILE_VERSION
        || (channels != 1u && channels != 3u)
        || levels == 0u
        || quant_step == 0u)
    {
        return DIC_HW4_FORMAT_ERROR;
    }

    encoded->channel_streams = (dic_basic_channel_stream *)calloc(
        (size_t)channels,
        sizeof(encoded->channel_streams[0])
    );
    if (encoded->channel_streams == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    encoded->width = (int)width;
    encoded->height = (int)height;
    encoded->channels = (int)channels;
    encoded->levels = (int)levels;
    encoded->quant_step = (int)quant_step;

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        status = dic_basic_read_channel(file, encoded->channel_streams + channel);
        if (status != DIC_STATUS_OK)
            break;
    }

    if (status != DIC_STATUS_OK)
        dic_basic_encoded_free(encoded);
    return status;
}

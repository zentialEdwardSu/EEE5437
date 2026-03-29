#include "hw2/hw2_binary_arthmetic.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum
{
    DIC_HW2_BINARY_ARITHMETIC_CODE_BITS = 32
};

static const uint64_t DIC_HW2_BINARY_ARITHMETIC_TOP = 0xFFFFFFFFULL;
static const uint64_t DIC_HW2_BINARY_ARITHMETIC_HALF = 0x80000000ULL;
static const uint64_t DIC_HW2_BINARY_ARITHMETIC_FIRST_QTR = 0x40000000ULL;
static const uint64_t DIC_HW2_BINARY_ARITHMETIC_THIRD_QTR = 0xC0000000ULL;
static const unsigned int DIC_HW2_BINARY_ARITHMETIC_SCALE = 16384U;

enum
{
    DIC_HW2_BINARY_ARITHMETIC_CONTEXT_START = 0,
    DIC_HW2_BINARY_ARITHMETIC_CONTEXT_PREV_ZERO = 1,
    DIC_HW2_BINARY_ARITHMETIC_CONTEXT_PREV_ONE = 2,
    DIC_HW2_BINARY_ARITHMETIC_CONTEXT_COUNT = 3
};

typedef struct dic_hw2_binary_arithmetic_bit_writer
{
    unsigned char *bytes;
    size_t bit_count;
    size_t capacity_bits;
} dic_hw2_binary_arithmetic_bit_writer;

typedef struct dic_hw2_binary_arithmetic_bit_reader
{
    const unsigned char *bytes;
    size_t bit_count;
    size_t bit_offset;
} dic_hw2_binary_arithmetic_bit_reader;

static unsigned char dic_hw2_get_bit(const unsigned char *bytes, size_t bit_offset)
{
    return (unsigned char)((bytes[bit_offset / 8] >> (7U - (unsigned int)(bit_offset % 8))) & 1U);
}

static void dic_hw2_set_bit(unsigned char *bytes, size_t bit_offset, unsigned char bit)
{
    if (bit != 0)
        bytes[bit_offset / 8] |= (unsigned char)(1U << (7U - (unsigned int)(bit_offset % 8)));
}

static dic_hw2_binary_arithmetic_status dic_hw2_binary_arithmetic_writer_init(
    dic_hw2_binary_arithmetic_bit_writer *writer,
    size_t capacity_bits
)
{
    writer->bytes = NULL;
    writer->bit_count = 0;
    writer->capacity_bits = capacity_bits;

    if (capacity_bits == 0)
        return DIC_HW2_BINARY_ARITHMETIC_OK;

    writer->bytes = (unsigned char *)calloc((capacity_bits + 7) / 8, sizeof(unsigned char));
    if (writer->bytes == NULL)
        return DIC_HW2_BINARY_ARITHMETIC_MEMORY_ERROR;

    return DIC_HW2_BINARY_ARITHMETIC_OK;
}

static dic_hw2_binary_arithmetic_status dic_hw2_binary_arithmetic_writer_put_bit(
    dic_hw2_binary_arithmetic_bit_writer *writer,
    unsigned char bit
)
{
    if (writer->bit_count >= writer->capacity_bits)
        return DIC_HW2_BINARY_ARITHMETIC_MEMORY_ERROR;

    dic_hw2_set_bit(writer->bytes, writer->bit_count, bit);
    ++writer->bit_count;
    return DIC_HW2_BINARY_ARITHMETIC_OK;
}

static dic_hw2_binary_arithmetic_status dic_hw2_binary_arithmetic_output_bit_plus_follow(
    dic_hw2_binary_arithmetic_bit_writer *writer,
    unsigned char bit,
    size_t *pending_bits
)
{
    dic_hw2_binary_arithmetic_status status;

    status = dic_hw2_binary_arithmetic_writer_put_bit(writer, bit);
    if (status != DIC_HW2_BINARY_ARITHMETIC_OK)
        return status;

    while (*pending_bits > 0)
    {
        status = dic_hw2_binary_arithmetic_writer_put_bit(writer, (unsigned char)(bit == 0 ? 1 : 0));
        if (status != DIC_HW2_BINARY_ARITHMETIC_OK)
            return status;
        --(*pending_bits);
    }

    return DIC_HW2_BINARY_ARITHMETIC_OK;
}

static void dic_hw2_binary_arithmetic_reader_init(
    dic_hw2_binary_arithmetic_bit_reader *reader,
    const unsigned char *bytes,
    size_t bit_count
)
{
    reader->bytes = bytes;
    reader->bit_count = bit_count;
    reader->bit_offset = 0;
}

static unsigned char dic_hw2_binary_arithmetic_reader_get_bit(dic_hw2_binary_arithmetic_bit_reader *reader)
{
    unsigned char bit = 0;

    if (reader->bit_offset < reader->bit_count)
        bit = dic_hw2_get_bit(reader->bytes, reader->bit_offset);

    ++reader->bit_offset;
    return bit;
}

static void dic_hw2_binary_arithmetic_update_range(
    uint64_t *low,
    uint64_t *high,
    const dic_hw2_binary_arithmetic_model *model,
    unsigned int context,
    unsigned char bit
)
{
    const uint64_t range = (*high - *low) + 1U;
    const uint64_t split = *low + ((range * model->freq0[context]) / model->total[context]);

    if (bit == 0)
    {
        *high = split - 1U;
    }
    else
    {
        *low = split;
    }
}

void dic_hw2_binary_arithmetic_bitstream_init(dic_hw2_binary_arithmetic_bitstream *bitstream)
{
    if (bitstream == NULL)
        return;

    bitstream->bytes = NULL;
    bitstream->byte_count = 0;
    bitstream->bit_count = 0;
}

void dic_hw2_binary_arithmetic_bitstream_free(dic_hw2_binary_arithmetic_bitstream *bitstream)
{
    if (bitstream == NULL)
        return;

    free(bitstream->bytes);
    dic_hw2_binary_arithmetic_bitstream_init(bitstream);
}

dic_hw2_binary_arithmetic_status dic_hw2_binary_arithmetic_build_model(
    const unsigned char *input,
    size_t input_size,
    dic_hw2_binary_arithmetic_model *model
)
{
    size_t bit_index = 0;
    size_t total_input_bits = input_size * 8U;
    size_t zero_count[DIC_HW2_BINARY_ARITHMETIC_CONTEXT_COUNT] = {0, 0, 0};
    size_t one_count[DIC_HW2_BINARY_ARITHMETIC_CONTEXT_COUNT] = {0, 0, 0};
    unsigned int context = DIC_HW2_BINARY_ARITHMETIC_CONTEXT_START;
    unsigned int i = 0;

    if (model == NULL)
        return DIC_HW2_BINARY_ARITHMETIC_INVALID_ARGUMENT;
    if (input_size > 0 && input == NULL)
        return DIC_HW2_BINARY_ARITHMETIC_INVALID_ARGUMENT;

    if (total_input_bits == 0)
    {
        for (i = 0; i < DIC_HW2_BINARY_ARITHMETIC_CONTEXT_COUNT; ++i)
        {
            model->freq0[i] = 1U;
            model->freq1[i] = 1U;
            model->total[i] = 2U;
        }
        return DIC_HW2_BINARY_ARITHMETIC_OK;
    }

    for (bit_index = 0; bit_index < total_input_bits; ++bit_index)
    {
        const unsigned char bit = dic_hw2_get_bit(input, bit_index);

        if (bit == 0)
            ++zero_count[context];
        else
            ++one_count[context];

        context = bit == 0
            ? DIC_HW2_BINARY_ARITHMETIC_CONTEXT_PREV_ZERO
            : DIC_HW2_BINARY_ARITHMETIC_CONTEXT_PREV_ONE;
    }

    for (i = 0; i < DIC_HW2_BINARY_ARITHMETIC_CONTEXT_COUNT; ++i)
    {
        const size_t total = zero_count[i] + one_count[i];
        unsigned int freq1 = 0;

        if (total == 0)
        {
            model->freq0[i] = 1U;
            model->freq1[i] = 1U;
            model->total[i] = 2U;
            continue;
        }

        freq1 = (unsigned int)((one_count[i] * DIC_HW2_BINARY_ARITHMETIC_SCALE + (total / 2U)) / total);
        if (freq1 == 0U)
            freq1 = 1U;
        if (freq1 >= DIC_HW2_BINARY_ARITHMETIC_SCALE)
            freq1 = DIC_HW2_BINARY_ARITHMETIC_SCALE - 1U;

        model->freq1[i] = freq1;
        model->freq0[i] = DIC_HW2_BINARY_ARITHMETIC_SCALE - freq1;
        model->total[i] = DIC_HW2_BINARY_ARITHMETIC_SCALE;
    }

    return DIC_HW2_BINARY_ARITHMETIC_OK;
}

dic_hw2_binary_arithmetic_status dic_hw2_binary_arithmetic_encode(
    const unsigned char *input,
    size_t input_size,
    const dic_hw2_binary_arithmetic_model *model,
    dic_hw2_binary_arithmetic_bitstream *bitstream
)
{
    uint64_t low = 0U;
    uint64_t high = DIC_HW2_BINARY_ARITHMETIC_TOP;
    size_t pending_bits = 0;
    size_t bit_index = 0;
    size_t total_input_bits = input_size * 8U;
    unsigned int context = DIC_HW2_BINARY_ARITHMETIC_CONTEXT_START;
    dic_hw2_binary_arithmetic_bit_writer writer;
    dic_hw2_binary_arithmetic_status status;

    if (model == NULL || bitstream == NULL)
        return DIC_HW2_BINARY_ARITHMETIC_INVALID_ARGUMENT;
    if (input_size > 0 && input == NULL)
        return DIC_HW2_BINARY_ARITHMETIC_INVALID_ARGUMENT;

    dic_hw2_binary_arithmetic_bitstream_free(bitstream);

    status = dic_hw2_binary_arithmetic_writer_init(
        &writer,
        total_input_bits + DIC_HW2_BINARY_ARITHMETIC_CODE_BITS + 64U
    );
    if (status != DIC_HW2_BINARY_ARITHMETIC_OK)
        return status;

    for (bit_index = 0; bit_index < total_input_bits; ++bit_index)
    {
        const unsigned char bit = dic_hw2_get_bit(input, bit_index);

        dic_hw2_binary_arithmetic_update_range(&low, &high, model, context, bit);

        for (;;)
        {
            if (high < DIC_HW2_BINARY_ARITHMETIC_HALF)
            {
                status = dic_hw2_binary_arithmetic_output_bit_plus_follow(&writer, 0, &pending_bits);
            }
            else if (low >= DIC_HW2_BINARY_ARITHMETIC_HALF)
            {
                status = dic_hw2_binary_arithmetic_output_bit_plus_follow(&writer, 1, &pending_bits);
                low -= DIC_HW2_BINARY_ARITHMETIC_HALF;
                high -= DIC_HW2_BINARY_ARITHMETIC_HALF;
            }
            else if (
                low >= DIC_HW2_BINARY_ARITHMETIC_FIRST_QTR &&
                high < DIC_HW2_BINARY_ARITHMETIC_THIRD_QTR
            )
            {
                ++pending_bits;
                low -= DIC_HW2_BINARY_ARITHMETIC_FIRST_QTR;
                high -= DIC_HW2_BINARY_ARITHMETIC_FIRST_QTR;
                status = DIC_HW2_BINARY_ARITHMETIC_OK;
            }
            else
            {
                break;
            }

            if (status != DIC_HW2_BINARY_ARITHMETIC_OK)
            {
                free(writer.bytes);
                return status;
            }

            low <<= 1U;
            high = (high << 1U) | 1U;
        }

        context = bit == 0
            ? DIC_HW2_BINARY_ARITHMETIC_CONTEXT_PREV_ZERO
            : DIC_HW2_BINARY_ARITHMETIC_CONTEXT_PREV_ONE;
    }

    ++pending_bits;
    if (low < DIC_HW2_BINARY_ARITHMETIC_FIRST_QTR)
        status = dic_hw2_binary_arithmetic_output_bit_plus_follow(&writer, 0, &pending_bits);
    else
        status = dic_hw2_binary_arithmetic_output_bit_plus_follow(&writer, 1, &pending_bits);

    if (status != DIC_HW2_BINARY_ARITHMETIC_OK)
    {
        free(writer.bytes);
        return status;
    }

    bitstream->bytes = writer.bytes;
    bitstream->bit_count = writer.bit_count;
    bitstream->byte_count = (writer.bit_count + 7U) / 8U;
    return DIC_HW2_BINARY_ARITHMETIC_OK;
}

dic_hw2_binary_arithmetic_status dic_hw2_binary_arithmetic_decode(
    const dic_hw2_binary_arithmetic_bitstream *bitstream,
    size_t output_size,
    const dic_hw2_binary_arithmetic_model *model,
    unsigned char *output
)
{
    uint64_t low = 0U;
    uint64_t high = DIC_HW2_BINARY_ARITHMETIC_TOP;
    uint64_t value = 0U;
    size_t bit_index = 0;
    size_t output_bits = output_size * 8U;
    unsigned int context = DIC_HW2_BINARY_ARITHMETIC_CONTEXT_START;
    dic_hw2_binary_arithmetic_bit_reader reader;

    if (bitstream == NULL || model == NULL)
        return DIC_HW2_BINARY_ARITHMETIC_INVALID_ARGUMENT;
    if (output_size > 0 && output == NULL)
        return DIC_HW2_BINARY_ARITHMETIC_INVALID_ARGUMENT;

    if (output_size > 0)
        memset(output, 0, output_size);

    dic_hw2_binary_arithmetic_reader_init(&reader, bitstream->bytes, bitstream->bit_count);

    for (bit_index = 0; bit_index < DIC_HW2_BINARY_ARITHMETIC_CODE_BITS; ++bit_index)
        value = (value << 1U) | dic_hw2_binary_arithmetic_reader_get_bit(&reader);

    for (bit_index = 0; bit_index < output_bits; ++bit_index)
    {
        const uint64_t range = (high - low) + 1U;
        const uint64_t split = low + ((range * model->freq0[context]) / model->total[context]);
        const unsigned char bit = (value < split) ? 0U : 1U;

        dic_hw2_set_bit(output, bit_index, bit);
        dic_hw2_binary_arithmetic_update_range(&low, &high, model, context, bit);

        for (;;)
        {
            if (high < DIC_HW2_BINARY_ARITHMETIC_HALF)
            {
            }
            else if (low >= DIC_HW2_BINARY_ARITHMETIC_HALF)
            {
                value -= DIC_HW2_BINARY_ARITHMETIC_HALF;
                low -= DIC_HW2_BINARY_ARITHMETIC_HALF;
                high -= DIC_HW2_BINARY_ARITHMETIC_HALF;
            }
            else if (
                low >= DIC_HW2_BINARY_ARITHMETIC_FIRST_QTR &&
                high < DIC_HW2_BINARY_ARITHMETIC_THIRD_QTR
            )
            {
                value -= DIC_HW2_BINARY_ARITHMETIC_FIRST_QTR;
                low -= DIC_HW2_BINARY_ARITHMETIC_FIRST_QTR;
                high -= DIC_HW2_BINARY_ARITHMETIC_FIRST_QTR;
            }
            else
            {
                break;
            }

            low <<= 1U;
            high = (high << 1U) | 1U;
            value = (value << 1U) | dic_hw2_binary_arithmetic_reader_get_bit(&reader);
        }

        context = bit == 0
            ? DIC_HW2_BINARY_ARITHMETIC_CONTEXT_PREV_ZERO
            : DIC_HW2_BINARY_ARITHMETIC_CONTEXT_PREV_ONE;
    }

    return DIC_HW2_BINARY_ARITHMETIC_OK;
}

dic_hw2_codec_status dic_hw2_binary_arithmetic_codec_backend(
    const unsigned char *input,
    size_t input_size,
    const dic_hw1_text_analysis *analysis,
    dic_hw2_codec_report *report
)
{
    dic_hw2_binary_arithmetic_model model;
    dic_hw2_binary_arithmetic_bitstream bitstream;
    unsigned char *decoded = NULL;
    dic_hw2_binary_arithmetic_status arithmetic_status;

    if (report == NULL)
        return DIC_HW2_CODEC_INVALID_ARGUMENT;
    if (input_size > 0 && input == NULL)
        return DIC_HW2_CODEC_INVALID_ARGUMENT;
    (void)analysis;

    dic_hw2_binary_arithmetic_bitstream_init(&bitstream);

    arithmetic_status = dic_hw2_binary_arithmetic_build_model(input, input_size, &model);
    if (arithmetic_status != DIC_HW2_BINARY_ARITHMETIC_OK)
        return DIC_HW2_CODEC_BACKEND_ERROR;

    arithmetic_status = dic_hw2_binary_arithmetic_encode(input, input_size, &model, &bitstream);
    if (arithmetic_status != DIC_HW2_BINARY_ARITHMETIC_OK)
        return DIC_HW2_CODEC_BACKEND_ERROR;

    if (input_size > 0)
    {
        decoded = (unsigned char *)malloc(input_size);
        if (decoded == NULL)
        {
            dic_hw2_binary_arithmetic_bitstream_free(&bitstream);
            return DIC_HW2_CODEC_MEMORY_ERROR;
        }
    }

    arithmetic_status = dic_hw2_binary_arithmetic_decode(&bitstream, input_size, &model, decoded);
    if (arithmetic_status != DIC_HW2_BINARY_ARITHMETIC_OK)
    {
        free(decoded);
        dic_hw2_binary_arithmetic_bitstream_free(&bitstream);
        return DIC_HW2_CODEC_BACKEND_ERROR;
    }

    report->original_bytes = input_size;
    report->decoded_bytes = input_size;
    report->encoded_bits = bitstream.bit_count;
    report->encoded_bytes = bitstream.byte_count;
    report->active_symbols = input_size > 0 ? DIC_HW2_BINARY_ARITHMETIC_CONTEXT_COUNT : 0U;
    report->max_code_bits = 0;
    report->average_code_length = input_size == 0 ? 0.0 : (double)bitstream.bit_count / (double)input_size;
    report->compression_ratio = input_size == 0 ? 0.0 : (double)bitstream.bit_count / (double)(input_size * 8U);
    report->compression_percent = input_size == 0 ? 0.0 : (1.0 - report->compression_ratio) * 100.0;
    report->roundtrip_matches = input_size == 0 || memcmp(input, decoded, input_size) == 0;

    free(decoded);
    dic_hw2_binary_arithmetic_bitstream_free(&bitstream);

    if (!report->roundtrip_matches)
        return DIC_HW2_CODEC_ROUNDTRIP_MISMATCH;

    return DIC_HW2_CODEC_OK;
}

const char *dic_hw2_binary_arithmetic_status_message(dic_hw2_binary_arithmetic_status status)
{
    switch (status)
    {
    case DIC_HW2_BINARY_ARITHMETIC_OK:
        return "ok";
    case DIC_HW2_BINARY_ARITHMETIC_INVALID_ARGUMENT:
        return "invalid argument";
    case DIC_HW2_BINARY_ARITHMETIC_MEMORY_ERROR:
        return "memory allocation failed";
    case DIC_HW2_BINARY_ARITHMETIC_MALFORMED_STREAM:
        return "malformed arithmetic stream";
    default:
        return "unknown error";
    }
}

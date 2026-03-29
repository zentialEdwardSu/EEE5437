#include "hw2/hw2_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *dic_hw2_open_binary_file(const char *path)
{
    FILE *input = NULL;

#if defined(_MSC_VER)
    if (fopen_s(&input, path, "rb") != 0)
        return NULL;
    return input;
#else
    return fopen(path, "rb");
#endif
}

static dic_hw2_codec_status dic_hw2_read_file_all(
    const char *path,
    unsigned char **buffer,
    size_t *size
)
{
    FILE *input = NULL;
    long length = 0;
    size_t read_size = 0;

    if (path == NULL || buffer == NULL || size == NULL)
        return DIC_HW2_CODEC_INVALID_ARGUMENT;

    *buffer = NULL;
    *size = 0;

    input = dic_hw2_open_binary_file(path);
    if (input == NULL)
        return DIC_HW2_CODEC_FILE_OPEN_ERROR;

    if (fseek(input, 0, SEEK_END) != 0)
    {
        fclose(input);
        return DIC_HW2_CODEC_FILE_READ_ERROR;
    }

    length = ftell(input);
    if (length < 0)
    {
        fclose(input);
        return DIC_HW2_CODEC_FILE_READ_ERROR;
    }

    if (fseek(input, 0, SEEK_SET) != 0)
    {
        fclose(input);
        return DIC_HW2_CODEC_FILE_READ_ERROR;
    }

    if (length == 0)
    {
        fclose(input);
        return DIC_HW2_CODEC_OK;
    }

    *buffer = (unsigned char *)malloc((size_t)length);
    if (*buffer == NULL)
    {
        fclose(input);
        return DIC_HW2_CODEC_MEMORY_ERROR;
    }

    read_size = fread(*buffer, 1, (size_t)length, input);
    if (read_size != (size_t)length || ferror(input) != 0)
    {
        free(*buffer);
        *buffer = NULL;
        fclose(input);
        return DIC_HW2_CODEC_FILE_READ_ERROR;
    }

    *size = read_size;
    fclose(input);
    return DIC_HW2_CODEC_OK;
}

static void dic_hw2_fill_probabilities(
    const dic_hw1_text_analysis *analysis,
    double probabilities[DIC_HW1_SYMBOL_COUNT]
)
{
    size_t symbol = 0;

    for (symbol = 0; symbol < DIC_HW1_SYMBOL_COUNT; ++symbol)
    {
        if (analysis->total_symbols == 0 || analysis->counts[symbol] == 0)
            probabilities[symbol] = 0.0;
        else
            probabilities[symbol] =
                (double)analysis->counts[symbol] / (double)analysis->total_symbols;
    }
}

static size_t dic_hw2_find_max_code_bits(const dic_hw2_huffman_tree *tree)
{
    size_t symbol = 0;
    size_t max_bits = 0;

    for (symbol = 0; symbol < tree->symbol_count; ++symbol)
    {
        if (tree->codes[symbol].bit_length > max_bits)
            max_bits = tree->codes[symbol].bit_length;
    }

    return max_bits;
}

dic_hw2_codec_status dic_hw2_huffman_codec_backend(
    const unsigned char *input,
    size_t input_size,
    const dic_hw1_text_analysis *analysis,
    dic_hw2_codec_report *report
)
{
    unsigned char *decoded = NULL;
    double probabilities[DIC_HW1_SYMBOL_COUNT];
    dic_hw2_huffman_tree tree;
    dic_hw2_huffman_bitstream bitstream;
    dic_hw2_huffman_status huffman_status;

    if (analysis == NULL || report == NULL)
        return DIC_HW2_CODEC_INVALID_ARGUMENT;

    if (input_size > 0 && input == NULL)
        return DIC_HW2_CODEC_INVALID_ARGUMENT;

    dic_hw2_huffman_tree_init(&tree);
    dic_hw2_huffman_bitstream_init(&bitstream);
    dic_hw2_fill_probabilities(analysis, probabilities);

    huffman_status = dic_hw2_byte_huffman_build(probabilities, DIC_HW1_SYMBOL_COUNT, &tree);
    if (huffman_status != DIC_HW2_HUFFMAN_OK)
        return DIC_HW2_CODEC_BACKEND_ERROR;

    huffman_status = dic_hw2_byte_huffman_encode(&tree, input, input_size, &bitstream);
    if (huffman_status != DIC_HW2_HUFFMAN_OK)
    {
        dic_hw2_huffman_tree_free(&tree);
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_HW2_CODEC_BACKEND_ERROR;
    }

    if (input_size > 0)
    {
        decoded = (unsigned char *)malloc(input_size);
        if (decoded == NULL)
        {
            dic_hw2_huffman_tree_free(&tree);
            dic_hw2_huffman_bitstream_free(&bitstream);
            return DIC_HW2_CODEC_MEMORY_ERROR;
        }
    }

    huffman_status = dic_hw2_byte_huffman_decode(&tree, &bitstream, input_size, decoded);
    if (huffman_status != DIC_HW2_HUFFMAN_OK)
    {
        free(decoded);
        dic_hw2_huffman_tree_free(&tree);
        dic_hw2_huffman_bitstream_free(&bitstream);
        return DIC_HW2_CODEC_BACKEND_ERROR;
    }

    report->original_bytes = input_size;
    report->decoded_bytes = input_size;
    report->encoded_bits = bitstream.bit_count;
    report->encoded_bytes = bitstream.byte_count;
    report->active_symbols = tree.active_symbols;
    report->max_code_bits = dic_hw2_find_max_code_bits(&tree);
    report->average_code_length = input_size == 0 ? 0.0 : (double)bitstream.bit_count / (double)input_size;
    report->compression_ratio = input_size == 0 ? 0.0 : (double)bitstream.bit_count / (double)(input_size * 8);
    report->compression_percent = input_size == 0 ? 0.0 : (1.0 - report->compression_ratio) * 100.0;
    report->roundtrip_matches = input_size == 0 || memcmp(input, decoded, input_size) == 0;

    free(decoded);
    dic_hw2_huffman_tree_free(&tree);
    dic_hw2_huffman_bitstream_free(&bitstream);

    if (!report->roundtrip_matches)
        return DIC_HW2_CODEC_ROUNDTRIP_MISMATCH;

    return DIC_HW2_CODEC_OK;
}

dic_hw2_codec_status dic_hw2_codec_run_file(
    const char *path,
    dic_hw2_codec_backend_fn backend,
    dic_hw2_codec_report *report
)
{
    unsigned char *original = NULL;
    size_t original_size = 0;
    dic_hw1_status analysis_status;
    dic_hw2_codec_status status;

    if (path == NULL || backend == NULL || report == NULL)
        return DIC_HW2_CODEC_INVALID_ARGUMENT;

    memset(report, 0, sizeof(*report));

    status = dic_hw2_read_file_all(path, &original, &original_size);
    if (status != DIC_HW2_CODEC_OK)
        return status;

    analysis_status = dic_hw1_analyze_text(original, original_size, &report->analysis);
    if (analysis_status != DIC_HW1_OK)
    {
        free(original);
        return DIC_HW2_CODEC_ANALYSIS_ERROR;
    }

    status = backend(original, original_size, &report->analysis, report);
    free(original);
    return status;
}

char *dic_hw2_codec_build_report(
    const char *path,
    const dic_hw2_codec_report *report
)
{
    char *buffer = NULL;
    int written = 0;

    if (path == NULL || report == NULL)
        return NULL;

    buffer = (char *)malloc(1024);
    if (buffer == NULL)
        return NULL;

    written = snprintf(
        buffer,
        1024,
        "input=%s\n"
        "original_bytes=%zu\n"
        "decoded_bytes=%zu\n"
        "unique_symbols=%zu\n"
        "active_symbols=%zu\n"
        "entropy=%.6f bits/symbol\n"
        "avg_code_length=%.6f bits/symbol\n"
        "max_code_bits=%zu\n"
        "encoded_bits=%zu\n"
        "encoded_bytes=%zu\n"
        "roundtrip=%s\n"
        "compression_ratio=%.6f\n"
        "compression_percent=%.2f%%\n",
        path,
        report->original_bytes,
        report->decoded_bytes,
        report->analysis.unique_symbols,
        report->active_symbols,
        report->analysis.entropy,
        report->average_code_length,
        report->max_code_bits,
        report->encoded_bits,
        report->encoded_bytes,
        report->roundtrip_matches ? "match" : "mismatch",
        report->compression_ratio,
        report->compression_percent
    );

    if (written < 0 || written >= 1024)
    {
        free(buffer);
        return NULL;
    }

    return buffer;
}

const char *dic_hw2_codec_status_message(dic_hw2_codec_status status)
{
    switch (status)
    {
    case DIC_HW2_CODEC_OK:
        return "ok";
    case DIC_HW2_CODEC_INVALID_ARGUMENT:
        return "invalid argument";
    case DIC_HW2_CODEC_FILE_OPEN_ERROR:
        return "failed to open input file";
    case DIC_HW2_CODEC_FILE_READ_ERROR:
        return "failed to read input file";
    case DIC_HW2_CODEC_MEMORY_ERROR:
        return "memory allocation failed";
    case DIC_HW2_CODEC_ANALYSIS_ERROR:
        return "failed to compute probability distribution";
    case DIC_HW2_CODEC_BACKEND_ERROR:
        return "codec backend failed";
    case DIC_HW2_CODEC_ROUNDTRIP_MISMATCH:
        return "decoded content does not match the original input";
    default:
        return "unknown error";
    }
}

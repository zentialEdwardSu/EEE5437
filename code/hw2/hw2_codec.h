#pragma once

#include "errors/errors.h"
#include "hw1/hw1_text_prob_analyzer.h"
#include "hw2/hw2_huffman.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_hw2_codec_report
{
    dic_hw1_text_analysis analysis;
    size_t original_bytes;
    size_t decoded_bytes;
    size_t encoded_bits;
    size_t encoded_bytes;
    size_t active_symbols;
    size_t max_code_bits;
    int roundtrip_matches;
    double average_code_length;
    double compression_ratio;
    double compression_percent;
} dic_hw2_codec_report;

typedef dic_status (*dic_hw2_codec_backend_fn)(
    const unsigned char *input,
    size_t input_size,
    const dic_hw1_text_analysis *analysis,
    dic_hw2_codec_report *report
);

DIC_REGISTER_HUFFMAN_CODEC(dic_hw2_byte_huffman, unsigned char)

dic_status dic_hw2_huffman_codec_backend(
    const unsigned char *input,
    size_t input_size,
    const dic_hw1_text_analysis *analysis,
    dic_hw2_codec_report *report
);

dic_status dic_hw2_binary_arithmetic_codec_backend(
    const unsigned char *input,
    size_t input_size,
    const dic_hw1_text_analysis *analysis,
    dic_hw2_codec_report *report
);

dic_status dic_hw2_codec_run_file(
    const char *path,
    dic_hw2_codec_backend_fn backend,
    dic_hw2_codec_report *report
);

char *dic_hw2_codec_build_report(
    const char *path,
    const dic_hw2_codec_report *report
);

#ifdef __cplusplus
}
#endif

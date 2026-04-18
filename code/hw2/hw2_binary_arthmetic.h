#pragma once

#include "errors/errors.h"
#include "hw1/hw1_text_prob_analyzer.h"
#include "hw2/hw2_codec.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_hw2_binary_arithmetic_model
{
    unsigned int freq0[3];
    unsigned int freq1[3];
    unsigned int total[3];
} dic_hw2_binary_arithmetic_model;

typedef struct dic_hw2_binary_arithmetic_bitstream
{
    unsigned char *bytes;
    size_t byte_count;
    size_t bit_count;
} dic_hw2_binary_arithmetic_bitstream;

void dic_hw2_binary_arithmetic_bitstream_init(dic_hw2_binary_arithmetic_bitstream *bitstream);
void dic_hw2_binary_arithmetic_bitstream_free(dic_hw2_binary_arithmetic_bitstream *bitstream);

dic_status dic_hw2_binary_arithmetic_build_model(
    const dic_hw1_text_analysis *analysis,
    dic_hw2_binary_arithmetic_model *model
);

dic_status dic_hw2_binary_arithmetic_encode(
    const unsigned char *input,
    size_t input_size,
    const dic_hw2_binary_arithmetic_model *model,
    dic_hw2_binary_arithmetic_bitstream *bitstream
);

dic_status dic_hw2_binary_arithmetic_decode(
    const dic_hw2_binary_arithmetic_bitstream *bitstream,
    size_t output_size,
    const dic_hw2_binary_arithmetic_model *model,
    unsigned char *output
);

dic_status dic_hw2_binary_arithmetic_codec_backend(
    const unsigned char *input,
    size_t input_size,
    const dic_hw1_text_analysis *analysis,
    dic_hw2_codec_report *report
);

#ifdef __cplusplus
}
#endif

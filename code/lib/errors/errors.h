#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define DIC_STATUS_TABLE(X)                                                     \
    X(DIC_STATUS_OK, 1000, "ok")                                                \
    X(DIC_STATUS_INVALID_ARGUMENT, 1001, "invalid argument")                    \
    X(DIC_STATUS_FILE_OPEN_ERROR, 1002, "failed to open input file")            \
    X(DIC_STATUS_FILE_READ_ERROR, 1003, "failed to read input file")            \
    X(DIC_STATUS_MEMORY_ERROR, 1004, "memory allocation failed")                \
    X(DIC_STATUS_IO_ERROR, 1005, "i/o error")                                   \
    X(DIC_HW2_CODEC_ANALYSIS_ERROR, 2005, "failed to analyze input statistics") \
    X(DIC_HW2_CODEC_BACKEND_ERROR, 2006, "codec backend failed")                \
    X(DIC_HW2_CODEC_ROUNDTRIP_MISMATCH, 2007, "decoded content does not match the original input") \
    X(DIC_HW2_HUFFMAN_INVALID_SYMBOL, 3003, "input contains a symbol without a Huffman code") \
    X(DIC_HW2_HUFFMAN_MALFORMED_BITSTREAM, 3004, "malformed Huffman bitstream") \
    X(DIC_HW2_BINARY_ARITHMETIC_MALFORMED_STREAM, 4003, "malformed arithmetic stream") \
    X(DIC_HW4_INVALID_DIMENSIONS, 5002, "invalid image dimensions")             \
    X(DIC_HW4_INVALID_CHANNELS, 5003, "only 8-bit 1-channel or 3-channel images are supported") \
    X(DIC_HW4_INVALID_LEVELS, 5004, "invalid wavelet decomposition level count") \
    X(DIC_HW4_INVALID_QUALITY, 5005, "quality must be in the range [1, 100]")   \
    X(DIC_HW4_FORMAT_ERROR, 5008, "invalid .dic53 file format")

#define DIC_STATUS_ENUM_ENTRY(symbol, value, message) symbol = value,
typedef enum dic_status
{
    DIC_STATUS_TABLE(DIC_STATUS_ENUM_ENTRY)
} dic_status;
#undef DIC_STATUS_ENUM_ENTRY

const char *dic_status_symbol(int status_code);
const char *dic_status_message(int status_code);
const char *dic_status_message_from_symbol(const char *status_symbol);

#ifdef __cplusplus
}
#endif

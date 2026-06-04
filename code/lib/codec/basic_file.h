#pragma once
/**
 * @file basic_file.h
 * @brief File and stream serialization for the progressive bitplane codec.
 *
 * @verbatim
 *  ┌─────────────────────────────────────────────────────────────────┐
 *  │                    DICW v2 Bitstream Layout                      │
 *  ├─────────────────────────────────────────────────────────────────┤
 *  │ Offset  Size      Field                                          │
 *  │ 0       4         magic[4] = "DICW"                             │
 *  │ 4       4         version (u32 LE) = 2                           │
 *  │ 8       4         width (u32 LE)                                 │
 *  │ 12      4         height (u32 LE)                                │
 *  │ 16      4         channels (u32 LE)                              │
 *  │ 20      4         levels (u32 LE)                                │
 *  │ 24      4         quant_step (u32 LE)                            │
 *  │ 28      4         num_bitplanes (u32 LE)  ← auto-detected        │
 *  ├─────────────────────────────────────────────────────────────────┤
 *  │  For each channel c in 0..channels-1:                           │
 *  │    For each bitplane bp in MSB..LSB (num_bitplanes total):      │
 *  │      u32  dominant_token_count                                   │
 *  │      u32  token_freq[4]     (IZ, ZTR, POS, NEG)                 │
 *  │      u32  dominant_bit_count                                     │
 *  │      u32  dominant_byte_count                                    │
 *  │      u8[] dominant_bytes     (Huffman-coded tokens)              │
 *  │      u32  subordinate_bit_count                                  │
 *  │      u32  subordinate_byte_count                                 │
 *  │      u8[] subordinate_bytes  (raw packed refinement bits)        │
 *  │      u32  end_marker = 0xFFFFFFFF  ← bitplane boundary sentinel  │
 *  └─────────────────────────────────────────────────────────────────┘
 * @endverbatim
 *
 * Progressive decoding is supported by reading only the first N bitplanes
 * via codec_basic_read_file_bitplanes().
 */

#include <stdio.h>

#include "codec/basic_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIC_BASIC_FILE_MAGIC "DICW"
#define DIC_BASIC_FILE_VERSION 2u
#define DIC_BP_END_MARKER 0xFFFFFFFFu  /** End-of-bitplane sentinel. */

/** Write full bitstream to file. */
dic_status codec_basic_write_file(const char *path, const codec_basic_encoded_image *encoded);
/** Read full bitstream from file. */
dic_status codec_basic_read_file(const char *path, codec_basic_encoded_image *encoded);
/** Read only the first num_bitplanes from file (progressive). */
dic_status codec_basic_read_file_bitplanes(
    const char *path, int num_bitplanes, codec_basic_encoded_image *encoded);

/** Write full bitstream to open stream. */
dic_status codec_basic_write_stream(FILE *file, const codec_basic_encoded_image *encoded);
/** Read full bitstream from open stream. */
dic_status codec_basic_read_stream(FILE *file, codec_basic_encoded_image *encoded);
/** Read first num_bitplanes from open stream (progressive). */
dic_status codec_basic_read_stream_bitplanes(
    FILE *file, int num_bitplanes, codec_basic_encoded_image *encoded);

#ifdef __cplusplus
}
#endif

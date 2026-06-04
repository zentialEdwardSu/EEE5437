#pragma once
/**
 * @file basic_file.h
 * @brief File and stream serialization for the progressive bitplane codec.
 *
 * @verbatim
 *  ┌─────────────────────────────────────────────────────────────────┐
 *  │                    DICW v3 Bitstream Layout                      │
 *  ├─────────────────────────────────────────────────────────────────┤
 *  │ Offset  Size      Field                                          │
 *  │ 0       4         magic[4] = "DICW"                             │
 *  │ 4       4         version (u32 LE) = 3                           │
 *  │ 8       4         width (u32 LE)                                 │
 *  │ 12      4         height (u32 LE)                                │
 *  │ 16      4         channels (u32 LE)                              │
 *  │ 20      4         levels (u32 LE)                                │
 *  │ 24      4         quant_step (u32 LE)                            │
 *  ├─────────────────────────────────────────────────────────────────┤
 *  │  For each channel c in 0..channels-1:                           │
 *  │    u32  num_resolutions = levels + 1                             │
 *  │    For each resolution r in 0..num_resolutions-1:                │
 *  │      u32  payload_byte_size  (total bytes for this resolution)   │
 *  │      u32  num_bitplanes                                          │
 *  │      For each bitplane bp in MSB..LSB:                           │
 *  │        u32  dominant_token_count                                  │
 *  │        u32  token_freq[4]                                         │
 *  │        u32  dominant_bit_count                                    │
 *  │        u32  dominant_byte_count                                   │
 *  │        u8[] dominant_bytes                                        │
 *  │        u32  subordinate_bit_count                                 │
 *  │        u32  subordinate_byte_count                                │
 *  │        u8[] subordinate_bytes                                     │
 *  │        u32  end_marker = 0xFFFFFFFF                               │
 *  └─────────────────────────────────────────────────────────────────┘
 * @endverbatim
 */

#include <stdio.h>

#include "codec/basic_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIC_BASIC_FILE_MAGIC "DICW"
#define DIC_BASIC_FILE_VERSION 3u
#define DIC_BP_END_MARKER 0xFFFFFFFFu

/** Write full bitstream to file. */
dic_status codec_basic_write_file(const char *path, const codec_basic_encoded_image *encoded);
/** Read full bitstream from file (all resolutions, all bitplanes). */
dic_status codec_basic_read_file(const char *path, codec_basic_encoded_image *encoded);

/** Read only resolutions 0..max_resolution from file, skipping the rest via fseek. */
dic_status codec_basic_read_file_resolution(
    const char *path, int max_resolution, codec_basic_encoded_image *encoded);

/** Write full bitstream to open stream. */
dic_status codec_basic_write_stream(FILE *file, const codec_basic_encoded_image *encoded);
/** Read full bitstream from open stream. */
dic_status codec_basic_read_stream(FILE *file, codec_basic_encoded_image *encoded);
/** Read resolutions 0..max_resolution from open stream. */
dic_status codec_basic_read_stream_resolution(
    FILE *file, int max_resolution, codec_basic_encoded_image *encoded);

#ifdef __cplusplus
}
#endif

#pragma once
/**
 * @file basic_file.h
 * @brief DICW v7 serialization for full-plane progressive streams.
 *
 * All integers are unsigned 32-bit little-endian values. The quantization
 * field stores the raw IEEE-754 binary32 bit pattern in a u32 slot.
 *
 * @code{.unparsed}
 * DICW v7 file
 * +-------------------------------+-------------------------------+
 * | Field                         | Size                          |
 * +-------------------------------+-------------------------------+
 * | magic "DICW"                  | 4 bytes                       |
 * | version                       | u32 LE                        |
 * | width                         | u32 LE                        |
 * | height                        | u32 LE                        |
 * | channels                      | u32 LE                        |
 * | DWT levels                    | u32 LE                        |
 * | quant_step IEEE-754 bits      | u32 LE                        |
 * | Huffman code lengths          | DIC_SCAN_TOKEN_COUNT bytes    |
 * +-------------------------------+-------------------------------+
 * | channel[0]                    | see channel block below       |
 * | ...                           |                               |
 * | channel[channels - 1]         |                               |
 * +-------------------------------+-------------------------------+
 *
 * channel block
 * +-------------------------------+
 * | bitplane_count : u32 LE       |
 * | bitplane[0] (most significant)|
 * | ...                           |
 * | bitplane[count - 1]           |
 * +-------------------------------+
 *
 * bitplane block
 * +--------------------------------------+------------------------+
 * | dominant_token_count                 | u32 LE                 |
 * | dominant_command_count               | u32 LE                 |
 * | dominant_bit_count                   | u32 LE                 |
 * | dominant_byte_count                  | u32 LE                 |
 * | dominant Huffman bytes               | dominant_byte_count    |
 * | run_bit_count                        | u32 LE                 |
 * | run_byte_count                       | u32 LE                 |
 * | Exp-Golomb run bytes                 | run_byte_count         |
 * | refinement_symbol_count              | u32 LE                 |
 * | refinement_mode                      | u32 LE                 |
 * | refinement_bit_count                 | u32 LE                 |
 * | refinement_byte_count                | u32 LE                 |
 * | refinement bytes                     | refinement_byte_count  |
 * | DIC_BP_END_MARKER (0xffffffff)       | u32 LE                 |
 * +--------------------------------------+------------------------+
 * @endcode
 */

#include <stdio.h>

#include "codec/basic_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Four-byte DICW container signature. */
#define DIC_BASIC_FILE_MAGIC "DICW"
/** Current incompatible DICW format version. */
#define DIC_BASIC_FILE_VERSION 7u
/** Terminator written after every serialized bit-plane block. */
#define DIC_BP_END_MARKER 0xFFFFFFFFu
/** Defensive upper bound accepted from untrusted streams. */
#define DIC_BASIC_MAX_LEVELS 32u
/** Defensive upper bound accepted per channel. */
#define DIC_BASIC_MAX_BITPLANES 32u

/**
 * @brief Computes the serialized byte size of one DICW bit-plane block.
 * @param bitplane Bit-plane descriptor.
 * @return Serialized bytes, including metadata and end marker, or zero for
 * NULL.
 */
size_t codec_basic_bitplane_byte_size(const codec_scan_bitplane* bitplane);

/**
 * @brief Computes the serialized byte size of one DICW channel block.
 * @param stream Channel stream.
 * @return Four-byte count plus all bit-plane blocks, or zero for NULL.
 */
size_t codec_basic_channel_byte_size(
    const codec_basic_channel_stream* stream);

/**
 * @brief Writes a complete DICW v7 file.
 * @param path Destination path, replaced if it exists.
 * @param encoded Valid encoded image.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_write_file(const char* path,
                                  const codec_basic_encoded_image* encoded);

/**
 * @brief Reads and validates a complete DICW v7 file.
 * @param path Source path.
 * @param encoded Destination owning object; prior contents are released.
 * @return DIC_STATUS_OK on success. Trailing bytes are rejected.
 */
dic_status codec_basic_read_file(const char* path,
                                 codec_basic_encoded_image* encoded);

/**
 * @brief Writes DICW data at the current FILE position.
 * @param file Writable binary stream. The function does not close it.
 * @param encoded Valid encoded image.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_write_stream(FILE* file,
                                    const codec_basic_encoded_image* encoded);

/**
 * @brief Reads DICW data from the current FILE position.
 * @param file Readable binary stream. The function stops after the last
 * channel and does not require EOF.
 * @param encoded Destination owning object.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_read_stream(FILE* file,
                                   codec_basic_encoded_image* encoded);

/**
 * @brief Serializes DICW v7 into a newly allocated byte buffer.
 * @param encoded Valid encoded image.
 * @param out_buffer Receives malloc-owned bytes; free with free().
 * @param out_size Receives the byte count.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_serialize(const codec_basic_encoded_image* encoded,
                                 uint8_t** out_buffer, size_t* out_size);

/**
 * @brief Deserializes exactly one DICW v7 object from memory.
 * @param buffer Source bytes.
 * @param size Exact source byte count; trailing bytes are rejected.
 * @param encoded Destination owning object.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_deserialize(const uint8_t* buffer, size_t size,
                                   codec_basic_encoded_image* encoded);

#ifdef __cplusplus
}
#endif

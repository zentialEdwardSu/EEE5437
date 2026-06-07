#pragma once
/**
 * @file basic_file.h
 * @brief DICW v8 layer-major serialization for progressive streams.
 *
 * All integers are unsigned 32-bit little-endian values. The quantization
 * field stores the raw IEEE-754 binary32 bit pattern in a u32 slot.
 *
 * @code{.unparsed}
 * DICW v8 file
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
 * | maximum quality layers        | u32 LE                        |
 * | bitplane count per channel    | channels x u32 LE             |
 * +-------------------------------+-------------------------------+
 * | layer 0: channel bit-planes   | channels owning layer 0       |
 * | layer marker 0xfffffffe       | u32 LE                        |
 * | layer 1: channel bit-planes   | channels owning layer 1       |
 * | layer marker 0xfffffffe       | u32 LE                        |
 * | ...                           |                               |
 * +-------------------------------+-------------------------------+
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
#define DIC_BASIC_FILE_VERSION 8u
/** Terminator written after every serialized bit-plane block. */
#define DIC_BP_END_MARKER 0xFFFFFFFFu
/** Terminator written after every complete quality layer. */
#define DIC_BASIC_LAYER_END_MARKER 0xFFFFFFFEu
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
 * @brief Computes one channel's count field plus all bit-plane blocks.
 * @param stream Channel stream.
 * @return Bytes attributed to the channel, or zero for NULL. In DICW v8 the
 * count is stored in the header and the bit-plane blocks are layer-major.
 */
size_t codec_basic_channel_byte_size(
    const codec_basic_channel_stream* stream);

/**
 * @brief Writes a complete DICW v8 file.
 * @param path Destination path, replaced if it exists.
 * @param encoded Valid encoded image.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_write_file(const char* path,
                                  const codec_basic_encoded_image* encoded);

/**
 * @brief Reads and validates a complete DICW v8 file.
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
 * quality layer and does not require EOF.
 * @param encoded Destination owning object.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_read_stream(FILE* file,
                                   codec_basic_encoded_image* encoded);

/**
 * @brief Serializes DICW v8 into a newly allocated byte buffer.
 * @param encoded Valid encoded image.
 * @param out_buffer Receives malloc-owned bytes; free with free().
 * @param out_size Receives the byte count.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_serialize(const codec_basic_encoded_image* encoded,
                                 uint8_t** out_buffer, size_t* out_size);

/**
 * @brief Deserializes exactly one DICW v8 object from memory.
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

/**
 * @file basic_file.c
 * @brief DICW v8 layer-major stream serialization.
 *
 * The implementation deliberately keeps byte-order conversion in the bits
 * module. All helpers here operate on complete logical DICW sections.
 */

#include "codec/basic_file.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/fs.h"
#include "bits/bits.h"

/**
 * @brief Returns the exact size of the bit-plane block shown below.
 *
 * @code{.unparsed}
 * fixed metadata: 11 x u32 = 44 bytes
 * + dominant bytes
 * + run bytes
 * + refinement bytes
 * @endcode
 */
size_t codec_basic_bitplane_byte_size(const codec_scan_bitplane* bitplane) {
    if (bitplane == NULL) return 0u;
    return 44u + bitplane->dominant_stream.byte_count +
           bitplane->run_length_byte_count +
           bitplane->subordinate_byte_count;
}

/**
 * @brief Returns the size of a channel count followed by its bit-planes.
 */
size_t codec_basic_channel_byte_size(
    const codec_basic_channel_stream* stream) {
    size_t size = 4u;
    int bp;
    if (stream == NULL) return 0u;
    for (bp = 0; bp < stream->num_bitplanes; ++bp)
        size += codec_basic_bitplane_byte_size(stream->bitplanes + bp);
    return size;
}

/**
 * @brief Writes one bit-plane block at the current file position.
 *
 * The function starts exactly at `dominant_token_count` and leaves the file
 * positioned immediately after @ref DIC_BP_END_MARKER.
 *
 * @code{.unparsed}
 * entry FILE position
 *   |
 *   v
 * +---------------------------+ 16 bytes
 * | token_count               | u32
 * | command_count             | u32
 * | dominant_bits             | u32
 * | dominant_bytes            | u32
 * +---------------------------+
 * | dominant payload          | dominant_bytes
 * +---------------------------+ 8 bytes
 * | run_bits                  | u32
 * | run_bytes                 | u32
 * +---------------------------+
 * | run payload               | run_bytes
 * +---------------------------+ 16 bytes
 * | refinement_symbols        | u32
 * | refinement_mode           | u32
 * | refinement_bits           | u32
 * | refinement_bytes          | u32
 * +---------------------------+
 * | refinement payload        | refinement_bytes
 * +---------------------------+ 4 bytes
 * | 0xffffffff end marker     | u32
 * +---------------------------+
 *                               ^
 *                               exit FILE position
 * @endcode
 */
static dic_status write_bitplane(FILE* file,
                                 const codec_scan_bitplane* bp) {
    uint32_t token_count, command_count, dominant_bits, dominant_bytes;
    uint32_t run_bits, run_bytes, refinement_symbols, refinement_bits;
    uint32_t refinement_bytes;
    if (!bits_size_to_u32(bp->dominant_token_count, &token_count) ||
        !bits_size_to_u32(bp->dominant_command_count, &command_count) ||
        !bits_size_to_u32(bp->dominant_stream.bit_count, &dominant_bits) ||
        !bits_size_to_u32(bp->dominant_stream.byte_count, &dominant_bytes) ||
        !bits_size_to_u32(bp->run_length_bit_count, &run_bits) ||
        !bits_size_to_u32(bp->run_length_byte_count, &run_bytes) ||
        !bits_size_to_u32(bp->subordinate_symbol_count, &refinement_symbols) ||
        !bits_size_to_u32(bp->subordinate_bit_count, &refinement_bits) ||
        !bits_size_to_u32(bp->subordinate_byte_count, &refinement_bytes))
        return DIC_STATUS_INVALID_ARGUMENT;

    if (!bits_write_u32(file, token_count) || !bits_write_u32(file, command_count) ||
        !bits_write_u32(file, dominant_bits) || !bits_write_u32(file, dominant_bytes) ||
        (dominant_bytes > 0u &&
         fwrite(bp->dominant_stream.bytes, 1u, dominant_bytes, file) !=
             dominant_bytes) ||
        !bits_write_u32(file, run_bits) || !bits_write_u32(file, run_bytes) ||
        (run_bytes > 0u &&
         fwrite(bp->run_length_bits, 1u, run_bytes, file) != run_bytes) ||
        !bits_write_u32(file, refinement_symbols) ||
        !bits_write_u32(file, (uint32_t)bp->subordinate_mode) ||
        !bits_write_u32(file, refinement_bits) ||
        !bits_write_u32(file, refinement_bytes) ||
        (refinement_bytes > 0u &&
         fwrite(bp->subordinate_bits, 1u, refinement_bytes, file) !=
             refinement_bytes) ||
        !bits_write_u32(file, DIC_BP_END_MARKER))
        return DIC_STATUS_IO_ERROR;
    return DIC_STATUS_OK;
}

/**
 * @brief Allocates and reads one variable-length payload at the cursor.
 *
 * @code{.unparsed}
 * FILE cursor -> [ byte_count payload bytes ] -> next field
 * @endcode
 *
 * A short read is treated as malformed DICW rather than a recoverable partial
 * stream because DICW file reads are non-incremental.
 */
static dic_status read_payload(FILE* file, uint32_t byte_count,
                               unsigned char** bytes) {
    *bytes = NULL;
    if (byte_count == 0u) return DIC_STATUS_OK;
    *bytes = (unsigned char*)malloc((size_t)byte_count);
    if (*bytes == NULL) return DIC_STATUS_MEMORY_ERROR;
    if (fread(*bytes, 1u, byte_count, file) != byte_count) {
        free(*bytes);
        *bytes = NULL;
        return DIC_HW4_FORMAT_ERROR;
    }
    return DIC_STATUS_OK;
}

/**
 * @brief Reads and validates one complete bit-plane block.
 *
 * The input cursor must point to the first count field shown in
 * write_bitplane(). On success it points immediately after the end marker.
 * @p plane_count bounds attacker-controlled token and refinement counts.
 */
static dic_status read_bitplane(FILE* file, size_t plane_count,
                                codec_scan_bitplane* bp) {
    uint32_t token_count, command_count, dominant_bits, dominant_bytes;
    uint32_t run_bits, run_bytes, symbols, mode, refinement_bits;
    uint32_t refinement_bytes, marker;
    dic_status status;

    codec_scan_bitplane_init(bp);
    if (!bits_read_u32(file, &token_count) || !bits_read_u32(file, &command_count) ||
        !bits_read_u32(file, &dominant_bits) || !bits_read_u32(file, &dominant_bytes))
        return DIC_HW4_FORMAT_ERROR;
    if ((size_t)token_count > plane_count ||
        command_count > token_count ||
        (uint64_t)dominant_bits > (uint64_t)dominant_bytes * 8u)
        return DIC_HW4_FORMAT_ERROR;
    bp->dominant_token_count = token_count;
    bp->dominant_command_count = command_count;
    bp->dominant_stream.bit_count = dominant_bits;
    bp->dominant_stream.byte_count = dominant_bytes;
    status = read_payload(file, dominant_bytes, &bp->dominant_stream.bytes);
    if (status != DIC_STATUS_OK) goto fail;

    if (!bits_read_u32(file, &run_bits) || !bits_read_u32(file, &run_bytes) ||
        (uint64_t)run_bits > (uint64_t)run_bytes * 8u) {
        status = DIC_HW4_FORMAT_ERROR;
        goto fail;
    }
    bp->run_length_bit_count = run_bits;
    bp->run_length_byte_count = run_bytes;
    status = read_payload(file, run_bytes, &bp->run_length_bits);
    if (status != DIC_STATUS_OK) goto fail;

    if (!bits_read_u32(file, &symbols) || !bits_read_u32(file, &mode) ||
        !bits_read_u32(file, &refinement_bits) ||
        !bits_read_u32(file, &refinement_bytes) ||
        symbols > plane_count || mode > DIC_SCAN_REFINEMENT_ARITHMETIC ||
        (uint64_t)refinement_bits > (uint64_t)refinement_bytes * 8u) {
        status = DIC_HW4_FORMAT_ERROR;
        goto fail;
    }
    bp->subordinate_symbol_count = symbols;
    bp->subordinate_mode = (codec_scan_refinement_mode)mode;
    bp->subordinate_bit_count = refinement_bits;
    bp->subordinate_byte_count = refinement_bytes;
    status =
        read_payload(file, refinement_bytes, &bp->subordinate_bits);
    if (status != DIC_STATUS_OK) goto fail;
    if (!bits_read_u32(file, &marker) || marker != DIC_BP_END_MARKER) {
        status = DIC_HW4_FORMAT_ERROR;
        goto fail;
    }
    return DIC_STATUS_OK;

fail:
    codec_scan_bitplane_free(bp);
    return status;
}

/**
 * @brief Validates metadata required before any bytes are serialized.
 */
static dic_status validate_encoded(
    const codec_basic_encoded_image* encoded) {
    int channel;
    if (encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (encoded->width <= 0 || encoded->height <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;
    if (encoded->channels != 1 && encoded->channels != 3)
        return DIC_HW4_INVALID_CHANNELS;
    if (encoded->levels <= 0 ||
        encoded->levels > (int)DIC_BASIC_MAX_LEVELS ||
        !isfinite(encoded->quant_step) || encoded->quant_step <= 0.0f)
        return DIC_STATUS_INVALID_ARGUMENT;
    for (channel = 0; channel < encoded->channels; ++channel) {
        const codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        if (stream->num_bitplanes < 0 ||
            stream->num_bitplanes > (int)DIC_BASIC_MAX_BITPLANES ||
            (stream->num_bitplanes > 0 && stream->bitplanes == NULL))
            return DIC_STATUS_INVALID_ARGUMENT;
    }
    return DIC_STATUS_OK;
}

/** @brief Finds the number of layer iterations required by all channels. */
static int maximum_bitplanes(const codec_basic_encoded_image* encoded) {
    int maximum = 0;
    int channel;
    for (channel = 0; channel < encoded->channels; ++channel) {
        int count = encoded->channel_streams[channel].num_bitplanes;
        if (count > maximum) maximum = count;
    }
    return maximum;
}

/**
 * @brief Writes a DICW header followed by layer-major bit-plane blocks.
 *
 * @code{.unparsed}
 * current cursor
 *   |
 *   +-> DICW fixed header
 *   +-> maximum layer count and all channel counts
 *   +-> layer 0: ch0 bp0, ch1 bp0, ... , 0xfffffffe
 *   `-> layer 1: ch0 bp1, ch1 bp1, ... , 0xfffffffe
 * @endcode
 */
dic_status codec_basic_write_stream(FILE* file,
                                    const codec_basic_encoded_image* encoded) {
    unsigned char lengths[DIC_SCAN_TOKEN_COUNT];
    dic_status status = validate_encoded(encoded);
    int maximum, channel, layer;
    if (file == NULL || status != DIC_STATUS_OK)
        return file == NULL ? DIC_STATUS_INVALID_ARGUMENT : status;
    maximum = maximum_bitplanes(encoded);
    codec_scan_get_code_lengths(lengths);
    if (fwrite(DIC_BASIC_FILE_MAGIC, 1u, 4u, file) != 4u ||
        !bits_write_u32(file, DIC_BASIC_FILE_VERSION) ||
        !bits_write_u32(file, (uint32_t)encoded->width) ||
        !bits_write_u32(file, (uint32_t)encoded->height) ||
        !bits_write_u32(file, (uint32_t)encoded->channels) ||
        !bits_write_u32(file, (uint32_t)encoded->levels) ||
        !bits_write_u32(file, bits_float_bits(encoded->quant_step)) ||
        fwrite(lengths, 1u, DIC_SCAN_TOKEN_COUNT, file) !=
            DIC_SCAN_TOKEN_COUNT ||
        !bits_write_u32(file, (uint32_t)maximum))
        return DIC_STATUS_IO_ERROR;

    for (channel = 0; channel < encoded->channels; ++channel) {
        const codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        if (!bits_write_u32(file, (uint32_t)stream->num_bitplanes))
            return DIC_STATUS_IO_ERROR;
    }
    for (layer = 0; layer < maximum; ++layer) {
        for (channel = 0; channel < encoded->channels; ++channel) {
            const codec_basic_channel_stream* stream =
                encoded->channel_streams + channel;
            if (layer >= stream->num_bitplanes) continue;
            status = write_bitplane(file, stream->bitplanes + layer);
            if (status != DIC_STATUS_OK) return status;
        }
        if (!bits_write_u32(file, DIC_BASIC_LAYER_END_MARKER))
            return DIC_STATUS_IO_ERROR;
    }
    return DIC_STATUS_OK;
}

/**
 * @brief Reads the DICW object beginning at the current stream cursor.
 *
 * This stream-level function intentionally does not test EOF. File and memory
 * wrappers perform exact-length checks after it returns.
 */
dic_status codec_basic_read_stream(FILE* file,
                                   codec_basic_encoded_image* encoded) {
    char magic[4];
    unsigned char lengths[DIC_SCAN_TOKEN_COUNT];
    uint32_t version, width, height, channels, levels, quant_bits, maximum;
    float quant;
    dic_status status;
    int channel, layer;
    size_t plane_count;

    if (file == NULL || encoded == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    codec_basic_encoded_free(encoded);
    if (fread(magic, 1u, 4u, file) != 4u ||
        memcmp(magic, DIC_BASIC_FILE_MAGIC, 4u) != 0 ||
        !bits_read_u32(file, &version) || !bits_read_u32(file, &width) ||
        !bits_read_u32(file, &height) || !bits_read_u32(file, &channels) ||
        !bits_read_u32(file, &levels) || !bits_read_u32(file, &quant_bits) ||
        fread(lengths, 1u, DIC_SCAN_TOKEN_COUNT, file) !=
            DIC_SCAN_TOKEN_COUNT ||
        !bits_read_u32(file, &maximum))
        return DIC_HW4_FORMAT_ERROR;
    quant = bits_float_from_bits(quant_bits);
    if (version != DIC_BASIC_FILE_VERSION || width == 0u ||
        width > INT_MAX || height == 0u || height > INT_MAX ||
        (channels != 1u && channels != 3u) || levels == 0u ||
        levels > DIC_BASIC_MAX_LEVELS || !isfinite(quant) || quant <= 0.0f ||
        maximum > DIC_BASIC_MAX_BITPLANES)
        return DIC_HW4_FORMAT_ERROR;

    codec_scan_set_code_lengths(lengths);
    status = codec_basic_encoded_alloc_streams(
        encoded, (int)width, (int)height, (int)channels, (int)levels, quant);
    if (status != DIC_STATUS_OK) return status;
    plane_count = (size_t)width * (size_t)height;

    for (channel = 0; channel < encoded->channels; ++channel) {
        uint32_t count;
        codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        if (!bits_read_u32(file, &count) || count > DIC_BASIC_MAX_BITPLANES) {
            status = DIC_HW4_FORMAT_ERROR;
            goto fail;
        }
        stream->num_bitplanes = (int)count;
        if (count == 0u) continue;
        stream->bitplanes = (codec_scan_bitplane*)calloc(
            count, sizeof(stream->bitplanes[0]));
        if (stream->bitplanes == NULL) {
            status = DIC_STATUS_MEMORY_ERROR;
            goto fail;
        }
    }
    if ((uint32_t)maximum_bitplanes(encoded) != maximum) {
        status = DIC_HW4_FORMAT_ERROR;
        goto fail;
    }
    for (layer = 0; layer < (int)maximum; ++layer) {
        for (channel = 0; channel < encoded->channels; ++channel) {
            codec_basic_channel_stream* stream =
                encoded->channel_streams + channel;
            if (layer >= stream->num_bitplanes) continue;
            status =
                read_bitplane(file, plane_count, stream->bitplanes + layer);
            if (status != DIC_STATUS_OK) goto fail;
        }
        {
            uint32_t marker;
            if (!bits_read_u32(file, &marker) ||
                marker != DIC_BASIC_LAYER_END_MARKER) {
                status = DIC_HW4_FORMAT_ERROR;
                goto fail;
            }
        }
    }
    return DIC_STATUS_OK;

fail:
    codec_basic_encoded_free(encoded);
    return status;
}

/** @brief Opens @p path and delegates DICW emission to the stream writer. */
dic_status codec_basic_write_file(const char* path,
                                  const codec_basic_encoded_image* encoded) {
    FILE* file;
    dic_status status;
    if (path == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    file = fs_open_file(path, "wb");
    if (file == NULL) return DIC_STATUS_IO_ERROR;
    status = codec_basic_write_stream(file, encoded);
    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/**
 * @brief Reads exactly one DICW file and rejects any trailing byte.
 */
dic_status codec_basic_read_file(const char* path,
                                 codec_basic_encoded_image* encoded) {
    FILE* file;
    dic_status status;
    if (path == NULL || encoded == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    file = fs_open_file(path, "rb");
    if (file == NULL) return DIC_STATUS_FILE_OPEN_ERROR;
    status = codec_basic_read_stream(file, encoded);
    if (status == DIC_STATUS_OK && fgetc(file) != EOF)
        status = DIC_HW4_FORMAT_ERROR;
    fclose(file);
    if (status != DIC_STATUS_OK) codec_basic_encoded_free(encoded);
    return status;
}

/**
 * @brief Uses a temporary file as the canonical FILE-to-memory adapter.
 *
 * @code{.unparsed}
 * encoded object -> write_stream(tmpfile) -> rewind -> malloc buffer
 * @endcode
 */
dic_status codec_basic_serialize(const codec_basic_encoded_image* encoded,
                                 uint8_t** out_buffer, size_t* out_size) {
    FILE* file;
    long length;
    uint8_t* buffer;
    dic_status status;
    if (out_buffer == NULL || out_size == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    *out_buffer = NULL;
    *out_size = 0u;
    file = fs_open_temp_file();
    if (file == NULL) return DIC_STATUS_IO_ERROR;
    status = codec_basic_write_stream(file, encoded);
    if (status != DIC_STATUS_OK || fflush(file) != 0 ||
        fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return status == DIC_STATUS_OK ? DIC_STATUS_IO_ERROR : status;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return DIC_STATUS_IO_ERROR;
    }
    buffer = length == 0 ? NULL : (uint8_t*)malloc((size_t)length);
    if (buffer == NULL && length > 0) {
        fclose(file);
        return DIC_STATUS_MEMORY_ERROR;
    }
    if (length > 0 &&
        fread(buffer, 1u, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return DIC_STATUS_IO_ERROR;
    }
    fclose(file);
    *out_buffer = buffer;
    *out_size = (size_t)length;
    return DIC_STATUS_OK;
}

/**
 * @brief Uses a temporary file as the canonical memory-to-FILE adapter.
 *
 * @code{.unparsed}
 * input buffer -> tmpfile -> rewind -> read_stream(encoded)
 * @endcode
 */
dic_status codec_basic_deserialize(const uint8_t* buffer, size_t size,
                                   codec_basic_encoded_image* encoded) {
    FILE* file;
    dic_status status;
    if (buffer == NULL || encoded == NULL || size > (size_t)LONG_MAX)
        return DIC_STATUS_INVALID_ARGUMENT;
    file = fs_open_temp_file();
    if (file == NULL) return DIC_STATUS_IO_ERROR;
    if (size > 0u && fwrite(buffer, 1u, size, file) != size) {
        fclose(file);
        return DIC_STATUS_IO_ERROR;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return DIC_STATUS_IO_ERROR;
    }
    status = codec_basic_read_stream(file, encoded);
    if (status == DIC_STATUS_OK && ftell(file) != (long)size)
        status = DIC_HW4_FORMAT_ERROR;
    fclose(file);
    if (status != DIC_STATUS_OK) codec_basic_encoded_free(encoded);
    return status;
}

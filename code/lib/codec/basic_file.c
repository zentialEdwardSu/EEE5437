/**
 * @file basic_file.c
 * @brief Implements DICW v6 serialization with per-resolution payload size
 * headers.
 *
 * The format includes a per-resolution payload_byte_size field that allows
 * partial reads to skip unneeded resolutions via fseek without parsing bitplane
 * headers.
 */

#include "codec/basic_file.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/fs.h"
#include "hw2/hw2_huffman.h"

static inline void u32_to_le_bytes(uint32_t value, unsigned char out[4]) {
    out[0] = (unsigned char)(value & 0xffu);
    out[1] = (unsigned char)((value >> 8) & 0xffu);
    out[2] = (unsigned char)((value >> 16) & 0xffu);
    out[3] = (unsigned char)((value >> 24) & 0xffu);
}

static inline uint32_t u32_from_le_bytes(const unsigned char in[4]) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

static uint32_t codec_basic_float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float codec_basic_float_from_bits(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int codec_basic_write_u32_le(FILE* file, uint32_t value) {
    unsigned char bytes[4];
    if (file == NULL) return 0;
    u32_to_le_bytes(value, bytes);
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes);
}

/* -------------------------------------------------------------------------- */
/*  Per-bitplane read (FILE path) */
/* -------------------------------------------------------------------------- */

static int codec_basic_read_u32_le(FILE* file, uint32_t* value) {
    unsigned char bytes[4];
    if (file == NULL || value == NULL) return 0;
    if (fread(bytes, 1u, sizeof(bytes), file) != sizeof(bytes)) return 0;
    *value = u32_from_le_bytes(bytes);
    return 1;
}

static dic_status codec_basic_read_bitplane(FILE* file,
                                            codec_scan_bitplane* bp) {
    uint32_t u32;

    codec_scan_bitplane_init(bp);

    if (!codec_basic_read_u32_le(file, &u32)) return DIC_HW4_FORMAT_ERROR;
    bp->dominant_token_count = (size_t)u32;

    if (!codec_basic_read_u32_le(file, &u32)) return DIC_HW4_FORMAT_ERROR;
    bp->dominant_stream.bit_count = (size_t)u32;
    if (!codec_basic_read_u32_le(file, &u32)) return DIC_HW4_FORMAT_ERROR;
    bp->dominant_stream.byte_count = (size_t)u32;

    if (bp->dominant_stream.byte_count > 0u) {
        bp->dominant_stream.bytes =
            (unsigned char*)malloc(bp->dominant_stream.byte_count);
        if (bp->dominant_stream.bytes == NULL) return DIC_STATUS_MEMORY_ERROR;
        if (fread(bp->dominant_stream.bytes, 1u, bp->dominant_stream.byte_count,
                  file) != bp->dominant_stream.byte_count) {
            dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
            return DIC_HW4_FORMAT_ERROR;
        }
    }

    if (!codec_basic_read_u32_le(file, &u32)) {
        dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
        return DIC_HW4_FORMAT_ERROR;
    }
    bp->subordinate_bit_count = (size_t)u32;
    if (!codec_basic_read_u32_le(file, &u32)) {
        dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
        return DIC_HW4_FORMAT_ERROR;
    }
    bp->subordinate_byte_count = (size_t)u32;

    if (bp->subordinate_byte_count > 0u) {
        bp->subordinate_bits =
            (unsigned char*)malloc(bp->subordinate_byte_count);
        if (bp->subordinate_bits == NULL) {
            dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
            return DIC_STATUS_MEMORY_ERROR;
        }
        if (fread(bp->subordinate_bits, 1u, bp->subordinate_byte_count, file) !=
            bp->subordinate_byte_count) {
            free(bp->subordinate_bits);
            dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
            return DIC_HW4_FORMAT_ERROR;
        }
    }

    /* Verify end-of-bitplane sentinel */
    if (!codec_basic_read_u32_le(file, &u32) || u32 != DIC_BP_END_MARKER) {
        dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
        free(bp->subordinate_bits);
        return DIC_HW4_FORMAT_ERROR;
    }

    return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Resolution payload size helper */
/* -------------------------------------------------------------------------- */

/** Compute serialized byte size of one resolution's data (excluding the 4-byte
 *  payload_byte_size field itself). */
size_t codec_basic_resolution_byte_size(
    const codec_basic_resolution_stream* rs) {
    size_t size = 4u; /* num_bitplanes u32 */
    int bp;
    for (bp = 0; bp < rs->num_bitplanes; ++bp) {
        const codec_scan_bitplane* cur = rs->bitplanes + bp;
        size += 4u + 4u + 4u;
        size += cur->dominant_stream.byte_count;
        size += 4u + 4u;
        size += cur->subordinate_byte_count;
        size += 4u; /* END_MARKER */
    }
    return size;
}

/* -------------------------------------------------------------------------- */
/*  Write                                                                     */
/* -------------------------------------------------------------------------- */

dic_status codec_basic_write_stream(FILE* file,
                                    const codec_basic_encoded_image* encoded) {
    int channel, res, bp;
    unsigned char code_lengths[DIC_SCAN_TOKEN_COUNT];

    if (file == NULL || encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (encoded->width <= 0 || encoded->height <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;
    if (encoded->channels != 1 && encoded->channels != 3)
        return DIC_HW4_INVALID_CHANNELS;
    if (encoded->levels <= 0 || !isfinite(encoded->quant_step) ||
        encoded->quant_step <= 0.0f)
        return DIC_STATUS_INVALID_ARGUMENT;

    codec_scan_get_code_lengths(code_lengths);
    if (fwrite(DIC_BASIC_FILE_MAGIC, 1u, 4u, file) != 4u ||
        !codec_basic_write_u32_le(file, DIC_BASIC_FILE_VERSION) ||
        !codec_basic_write_u32_le(file, (uint32_t)encoded->width) ||
        !codec_basic_write_u32_le(file, (uint32_t)encoded->height) ||
        !codec_basic_write_u32_le(file, (uint32_t)encoded->channels) ||
        !codec_basic_write_u32_le(file, (uint32_t)encoded->levels) ||
        !codec_basic_write_u32_le(file,
                                  codec_basic_float_bits(encoded->quant_step)) ||
        fwrite(code_lengths, 1u, DIC_SCAN_TOKEN_COUNT, file) !=
            DIC_SCAN_TOKEN_COUNT ||
        !codec_basic_write_u32_le(file, (uint32_t)encoded->color_transform))
        return DIC_STATUS_IO_ERROR;

    for (channel = 0; channel < encoded->channels; ++channel) {
        const codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        if (!codec_basic_write_u32_le(file, (uint32_t)stream->num_resolutions))
            return DIC_STATUS_IO_ERROR;

        for (res = 0; res < stream->num_resolutions; ++res) {
            const codec_basic_resolution_stream* rs = stream->resolutions + res;
            size_t payload_size = codec_basic_resolution_byte_size(rs);
            if (payload_size > UINT32_MAX ||
                !codec_basic_write_u32_le(file, (uint32_t)payload_size) ||
                !codec_basic_write_u32_le(file, (uint32_t)rs->num_bitplanes))
                return DIC_STATUS_IO_ERROR;

            for (bp = 0; bp < rs->num_bitplanes; ++bp) {
                const codec_scan_bitplane* cur = rs->bitplanes + bp;
                if (cur->dominant_token_count > UINT32_MAX ||
                    cur->dominant_stream.bit_count > UINT32_MAX ||
                    cur->dominant_stream.byte_count > UINT32_MAX ||
                    cur->subordinate_bit_count > UINT32_MAX ||
                    cur->subordinate_byte_count > UINT32_MAX ||
                    !codec_basic_write_u32_le(
                        file, (uint32_t)cur->dominant_token_count) ||
                    !codec_basic_write_u32_le(
                        file, (uint32_t)cur->dominant_stream.bit_count) ||
                    !codec_basic_write_u32_le(
                        file, (uint32_t)cur->dominant_stream.byte_count) ||
                    (cur->dominant_stream.byte_count > 0u &&
                     fwrite(cur->dominant_stream.bytes, 1u,
                            cur->dominant_stream.byte_count,
                            file) != cur->dominant_stream.byte_count) ||
                    !codec_basic_write_u32_le(
                        file, (uint32_t)cur->subordinate_bit_count) ||
                    !codec_basic_write_u32_le(
                        file, (uint32_t)cur->subordinate_byte_count) ||
                    (cur->subordinate_byte_count > 0u &&
                     fwrite(cur->subordinate_bits, 1u,
                            cur->subordinate_byte_count,
                            file) != cur->subordinate_byte_count) ||
                    !codec_basic_write_u32_le(file, DIC_BP_END_MARKER))
                    return DIC_STATUS_IO_ERROR;
            }
        }
    }

    return DIC_STATUS_OK;
}

dic_status codec_basic_write_file(const char* path,
                                  const codec_basic_encoded_image* encoded) {
    FILE* file = NULL;
    dic_status status;

    if (path == NULL || encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = fs_open_file(path, "wb");
    if (file == NULL) return DIC_STATUS_IO_ERROR;

    status = codec_basic_write_stream(file, encoded);
    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* -------------------------------------------------------------------------- */
/*  Header read                                                               */
/* -------------------------------------------------------------------------- */

static dic_status codec_basic_read_header(FILE* file,
                                          codec_basic_encoded_image* encoded) {
    char magic[4];
    uint32_t version, width, height, channels, levels, quant_step_bits,
        color_transform;
    float quant_step;
    unsigned char code_lengths[DIC_SCAN_TOKEN_COUNT];

    if (fread(magic, 1u, sizeof(magic), file) != sizeof(magic) ||
        memcmp(magic, DIC_BASIC_FILE_MAGIC, sizeof(magic)) != 0 ||
        !codec_basic_read_u32_le(file, &version) ||
        !codec_basic_read_u32_le(file, &width) ||
        !codec_basic_read_u32_le(file, &height) ||
        !codec_basic_read_u32_le(file, &channels) ||
        !codec_basic_read_u32_le(file, &levels) ||
        !codec_basic_read_u32_le(file, &quant_step_bits) ||
        fread(code_lengths, 1u, DIC_SCAN_TOKEN_COUNT, file) !=
            DIC_SCAN_TOKEN_COUNT ||
        !codec_basic_read_u32_le(file, &color_transform)) {
        return DIC_HW4_FORMAT_ERROR;
    }

    if (version != DIC_BASIC_FILE_VERSION &&
        version != DIC_BASIC_FILE_INTEGER_QUANT_VERSION)
        return DIC_HW4_FORMAT_ERROR;

    quant_step =
        version == DIC_BASIC_FILE_INTEGER_QUANT_VERSION
            ? (float)quant_step_bits
            : codec_basic_float_from_bits(quant_step_bits);

    if (
        (channels != 1u && channels != 3u) || levels == 0u ||
        levels > DIC_BASIC_MAX_LEVELS || !isfinite(quant_step) ||
        quant_step <= 0.0f ||
        color_transform > 1u || (color_transform == 1u && channels != 3u)) {
        return DIC_HW4_FORMAT_ERROR;
    }

    codec_scan_set_code_lengths(code_lengths);

    encoded->width = (int)width;
    encoded->height = (int)height;
    encoded->channels = (int)channels;
    encoded->levels = (int)levels;
    encoded->quant_step = quant_step;
    encoded->color_transform = (int)color_transform;
    return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Read (internal helper — resolution-limited) */
/* -------------------------------------------------------------------------- */

static dic_status codec_basic_read_stream_resolution_internal(
    FILE* file, int max_resolution, codec_basic_encoded_image* encoded) {
    dic_status status;
    int channel, res;

    if (file == NULL || encoded == NULL) return DIC_STATUS_INVALID_ARGUMENT;

    codec_basic_encoded_free(encoded);

    status = codec_basic_read_header(file, encoded);
    if (status != DIC_STATUS_OK) return status;

    encoded->channel_streams = (codec_basic_channel_stream*)calloc(
        (size_t)encoded->channels, sizeof(encoded->channel_streams[0]));
    if (encoded->channel_streams == NULL) return DIC_STATUS_MEMORY_ERROR;

    for (channel = 0; channel < encoded->channels; ++channel) {
        uint32_t num_res;
        codec_basic_channel_stream* stream = encoded->channel_streams + channel;

        if (!codec_basic_read_u32_le(file, &num_res)) {
            codec_basic_encoded_free(encoded);
            return DIC_HW4_FORMAT_ERROR;
        }
        stream->num_resolutions = (int)num_res;
        stream->resolutions = (codec_basic_resolution_stream*)calloc(
            (size_t)num_res, sizeof(stream->resolutions[0]));
        if (stream->resolutions == NULL) {
            codec_basic_encoded_free(encoded);
            return DIC_STATUS_MEMORY_ERROR;
        }

        for (res = 0; res < (int)num_res; ++res) {
            uint32_t payload_size, bp_count;
            codec_basic_resolution_stream* rs = stream->resolutions + res;

            if (!codec_basic_read_u32_le(file, &payload_size) ||
                !codec_basic_read_u32_le(file, &bp_count)) {
                codec_basic_encoded_free(encoded);
                return DIC_HW4_FORMAT_ERROR;
            }
            rs->resolution = res;

            if (res <= max_resolution) {
                int bp;
                rs->num_bitplanes = (int)bp_count;
                if (bp_count > 0u) {
                    rs->bitplanes = (codec_scan_bitplane*)calloc(
                        (size_t)bp_count, sizeof(rs->bitplanes[0]));
                    if (rs->bitplanes == NULL) {
                        codec_basic_encoded_free(encoded);
                        return DIC_STATUS_MEMORY_ERROR;
                    }
                    for (bp = 0; bp < (int)bp_count; ++bp) {
                        status =
                            codec_basic_read_bitplane(file, rs->bitplanes + bp);
                        if (status != DIC_STATUS_OK) {
                            codec_basic_encoded_free(encoded);
                            return status;
                        }
                    }
                }
            } else {
                /* Skip this resolution: payload_size covers num_bitplanes
                 * (already read) + all bitplane data.  Subtract the 4 bytes we
                 * consumed. */
                long skip = (long)(payload_size - 4u);
                if (skip > 0) {
                    if (fseek(file, skip, SEEK_CUR) != 0) {
                        codec_basic_encoded_free(encoded);
                        return DIC_STATUS_IO_ERROR;
                    }
                }
                rs->num_bitplanes = 0;
                rs->bitplanes = NULL;
            }
        }
    }

    return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Public read API                                                           */
/* -------------------------------------------------------------------------- */

dic_status codec_basic_read_stream(FILE* file,
                                   codec_basic_encoded_image* encoded) {
    return codec_basic_read_stream_resolution_internal(file, INT_MAX, encoded);
}

dic_status codec_basic_read_stream_resolution(
    FILE* file, int max_resolution, codec_basic_encoded_image* encoded) {
    return codec_basic_read_stream_resolution_internal(file, max_resolution,
                                                       encoded);
}

dic_status codec_basic_read_file(const char* path,
                                 codec_basic_encoded_image* encoded) {
    FILE* file = NULL;
    dic_status status;

    if (path == NULL || encoded == NULL) return DIC_STATUS_INVALID_ARGUMENT;

    file = fs_open_file(path, "rb");
    if (file == NULL) return DIC_STATUS_FILE_OPEN_ERROR;

    status = codec_basic_read_stream(file, encoded);
    fclose(file);
    if (status != DIC_STATUS_OK) codec_basic_encoded_free(encoded);
    return status;
}

dic_status codec_basic_read_file_resolution(
    const char* path, int max_resolution, codec_basic_encoded_image* encoded) {
    FILE* file = NULL;
    dic_status status;

    if (path == NULL || encoded == NULL) return DIC_STATUS_INVALID_ARGUMENT;

    file = fs_open_file(path, "rb");
    if (file == NULL) return DIC_STATUS_FILE_OPEN_ERROR;

    status = codec_basic_read_stream_resolution(file, max_resolution, encoded);
    fclose(file);
    if (status != DIC_STATUS_OK) codec_basic_encoded_free(encoded);
    return status;
}

/* -------------------------------------------------------------------------- */
/*  In-memory serialization helpers                                           */
/* -------------------------------------------------------------------------- */

#define SER_BUF_INITIAL_CAPACITY 256u

typedef struct ser_buf {
    uint8_t* data;
    size_t size;
    size_t capacity;
} ser_buf;

static void ser_buf_init(ser_buf* sb) {
    sb->data = NULL;
    sb->size = 0u;
    sb->capacity = 0u;
}

static void ser_buf_free(ser_buf* sb) {
    free(sb->data);
    sb->data = NULL;
    sb->size = 0u;
    sb->capacity = 0u;
}

static int ser_buf_grow(ser_buf* sb, size_t extra) {
    size_t needed;
    size_t new_cap;
    uint8_t* new_data;

    if (extra > SIZE_MAX - sb->size) return 0; /* overflow guard */
    needed = sb->size + extra;

    if (needed <= sb->capacity) return 1;

    new_cap = sb->capacity > 0u ? sb->capacity : SER_BUF_INITIAL_CAPACITY;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2u) return 0;
        new_cap *= 2u;
    }

    new_data = (uint8_t*)realloc(sb->data, new_cap);
    if (new_data == NULL) return 0;

    sb->data = new_data;
    sb->capacity = new_cap;
    return 1;
}

static int ser_buf_write_u32_le(ser_buf* sb, uint32_t value) {
    unsigned char bytes[4];
    if (!ser_buf_grow(sb, 4u)) return 0;
    u32_to_le_bytes(value, bytes);
    memcpy(sb->data + sb->size, bytes, 4u);
    sb->size += 4u;
    return 1;
}

static int ser_buf_write_bytes(ser_buf* sb, const void* data, size_t len) {
    if (len == 0u) return 1;
    if (!ser_buf_grow(sb, len)) return 0;
    memcpy(sb->data + sb->size, data, len);
    sb->size += len;
    return 1;
}

/* -------------------------------------------------------------------------- */
/*  In-memory deserialization helpers                                         */
/* -------------------------------------------------------------------------- */

typedef struct deser_buf {
    const uint8_t* data;
    size_t size;
    size_t pos;
} deser_buf;

static void deser_buf_init(deser_buf* db, const uint8_t* data, size_t size) {
    db->data = data;
    db->size = size;
    db->pos = 0u;
}

static int deser_buf_read_u32_le(deser_buf* db, uint32_t* value) {
    unsigned char bytes[4];
    if (db->pos + 4u > db->size) return 0;
    memcpy(bytes, db->data + db->pos, 4u);
    *value = u32_from_le_bytes(bytes);
    db->pos += 4u;
    return 1;
}

static int deser_buf_read_bytes(deser_buf* db, void* out, size_t len) {
    if (len == 0u) return 1;
    if (db->pos + len > db->size) return 0;
    memcpy(out, db->data + db->pos, len);
    db->pos += len;
    return 1;
}

/* -------------------------------------------------------------------------- */
/*  Per-bitplane serialize/deserialize against buffers                        */
/* -------------------------------------------------------------------------- */

static dic_status codec_basic_serialize_bitplane(
    ser_buf* sb, const codec_scan_bitplane* bp) {
    if (!ser_buf_write_u32_le(sb, (uint32_t)bp->dominant_token_count))
        return DIC_STATUS_MEMORY_ERROR;

    if (!ser_buf_write_u32_le(sb, (uint32_t)bp->dominant_stream.bit_count))
        return DIC_STATUS_MEMORY_ERROR;
    if (!ser_buf_write_u32_le(sb, (uint32_t)bp->dominant_stream.byte_count))
        return DIC_STATUS_MEMORY_ERROR;
    if (bp->dominant_stream.byte_count > 0u) {
        if (!ser_buf_write_bytes(sb, bp->dominant_stream.bytes,
                                 bp->dominant_stream.byte_count))
            return DIC_STATUS_MEMORY_ERROR;
    }

    if (!ser_buf_write_u32_le(sb, (uint32_t)bp->subordinate_bit_count))
        return DIC_STATUS_MEMORY_ERROR;
    if (!ser_buf_write_u32_le(sb, (uint32_t)bp->subordinate_byte_count))
        return DIC_STATUS_MEMORY_ERROR;
    if (bp->subordinate_byte_count > 0u) {
        if (!ser_buf_write_bytes(sb, bp->subordinate_bits,
                                 bp->subordinate_byte_count))
            return DIC_STATUS_MEMORY_ERROR;
    }

    if (!ser_buf_write_u32_le(sb, DIC_BP_END_MARKER))
        return DIC_STATUS_MEMORY_ERROR;

    return DIC_STATUS_OK;
}

static dic_status codec_basic_deserialize_bitplane(deser_buf* db,
                                                   codec_scan_bitplane* bp) {
    uint32_t u32;

    codec_scan_bitplane_init(bp);

    if (!deser_buf_read_u32_le(db, &u32)) return DIC_HW4_FORMAT_ERROR;
    bp->dominant_token_count = (size_t)u32;

    if (!deser_buf_read_u32_le(db, &u32)) return DIC_HW4_FORMAT_ERROR;
    bp->dominant_stream.bit_count = (size_t)u32;
    if (!deser_buf_read_u32_le(db, &u32)) return DIC_HW4_FORMAT_ERROR;
    bp->dominant_stream.byte_count = (size_t)u32;

    if (bp->dominant_stream.byte_count > 0u) {
        bp->dominant_stream.bytes =
            (unsigned char*)malloc(bp->dominant_stream.byte_count);
        if (bp->dominant_stream.bytes == NULL) return DIC_STATUS_MEMORY_ERROR;
        if (!deser_buf_read_bytes(db, bp->dominant_stream.bytes,
                                  bp->dominant_stream.byte_count)) {
            dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
            return DIC_HW4_FORMAT_ERROR;
        }
    }

    if (!deser_buf_read_u32_le(db, &u32)) {
        dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
        return DIC_HW4_FORMAT_ERROR;
    }
    bp->subordinate_bit_count = (size_t)u32;
    if (!deser_buf_read_u32_le(db, &u32)) {
        dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
        return DIC_HW4_FORMAT_ERROR;
    }
    bp->subordinate_byte_count = (size_t)u32;

    if (bp->subordinate_byte_count > 0u) {
        bp->subordinate_bits =
            (unsigned char*)malloc(bp->subordinate_byte_count);
        if (bp->subordinate_bits == NULL) {
            dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
            return DIC_STATUS_MEMORY_ERROR;
        }
        if (!deser_buf_read_bytes(db, bp->subordinate_bits,
                                  bp->subordinate_byte_count)) {
            free(bp->subordinate_bits);
            dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
            return DIC_HW4_FORMAT_ERROR;
        }
    }

    /* Verify end-of-bitplane sentinel */
    if (!deser_buf_read_u32_le(db, &u32) || u32 != DIC_BP_END_MARKER) {
        dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
        free(bp->subordinate_bits);
        return DIC_HW4_FORMAT_ERROR;
    }

    return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Public serialize / deserialize                                            */
/* -------------------------------------------------------------------------- */

dic_status codec_basic_serialize(const codec_basic_encoded_image* encoded,
                                 uint8_t** out_buffer, size_t* out_size) {
    ser_buf sb;
    dic_status status = DIC_STATUS_OK;
    int channel, res, bp;

    if (encoded == NULL || encoded->channel_streams == NULL ||
        out_buffer == NULL || out_size == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (encoded->width <= 0 || encoded->height <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;
    if (encoded->channels != 1 && encoded->channels != 3)
        return DIC_HW4_INVALID_CHANNELS;
    if (encoded->levels <= 0 || !isfinite(encoded->quant_step) ||
        encoded->quant_step <= 0.0f)
        return DIC_STATUS_INVALID_ARGUMENT;

    ser_buf_init(&sb);

    /* Header */
    if (!ser_buf_write_bytes(&sb, DIC_BASIC_FILE_MAGIC, 4u) ||
        !ser_buf_write_u32_le(&sb, DIC_BASIC_FILE_VERSION) ||
        !ser_buf_write_u32_le(&sb, (uint32_t)encoded->width) ||
        !ser_buf_write_u32_le(&sb, (uint32_t)encoded->height) ||
        !ser_buf_write_u32_le(&sb, (uint32_t)encoded->channels) ||
        !ser_buf_write_u32_le(&sb, (uint32_t)encoded->levels) ||
        !ser_buf_write_u32_le(&sb,
                              codec_basic_float_bits(encoded->quant_step))) {
        ser_buf_free(&sb);
        return DIC_STATUS_MEMORY_ERROR;
    }

    /* Fixed Huffman code lengths (4 × u8) */
    {
        unsigned char code_lengths[DIC_SCAN_TOKEN_COUNT];
        codec_scan_get_code_lengths(code_lengths);
        if (!ser_buf_write_bytes(&sb, code_lengths, DIC_SCAN_TOKEN_COUNT)) {
            ser_buf_free(&sb);
            return DIC_STATUS_MEMORY_ERROR;
        }
    }

    /* Color transform (u32 LE) */
    if (!ser_buf_write_u32_le(&sb, (uint32_t)encoded->color_transform)) {
        ser_buf_free(&sb);
        return DIC_STATUS_MEMORY_ERROR;
    }

    for (channel = 0; channel < encoded->channels; ++channel) {
        const codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        if (!ser_buf_write_u32_le(&sb, (uint32_t)stream->num_resolutions)) {
            ser_buf_free(&sb);
            return DIC_STATUS_MEMORY_ERROR;
        }
        for (res = 0; res < stream->num_resolutions; ++res) {
            const codec_basic_resolution_stream* rs = stream->resolutions + res;
            size_t payload_size = codec_basic_resolution_byte_size(rs);
            if (!ser_buf_write_u32_le(&sb, (uint32_t)payload_size) ||
                !ser_buf_write_u32_le(&sb, (uint32_t)rs->num_bitplanes)) {
                ser_buf_free(&sb);
                return DIC_STATUS_MEMORY_ERROR;
            }
            for (bp = 0; bp < rs->num_bitplanes; ++bp) {
                status =
                    codec_basic_serialize_bitplane(&sb, rs->bitplanes + bp);
                if (status != DIC_STATUS_OK) {
                    ser_buf_free(&sb);
                    return status;
                }
            }
        }
    }

    *out_buffer = sb.data;
    *out_size = sb.size;
    return DIC_STATUS_OK;
}

dic_status codec_basic_deserialize(const uint8_t* buffer, size_t size,
                                   codec_basic_encoded_image* encoded) {
    deser_buf db;
    dic_status status;
    uint32_t version, width, height, channels, levels, quant_step_bits,
        color_transform;
    float quant_step;
    unsigned char code_lengths[DIC_SCAN_TOKEN_COUNT];
    char magic[4];
    int channel, res;

    if (buffer == NULL || encoded == NULL) return DIC_STATUS_INVALID_ARGUMENT;

    codec_basic_encoded_free(encoded);
    deser_buf_init(&db, buffer, size);

    /* Read and validate header */
    if (!deser_buf_read_bytes(&db, magic, 4u) ||
        memcmp(magic, DIC_BASIC_FILE_MAGIC, 4u) != 0 ||
        !deser_buf_read_u32_le(&db, &version) ||
        !deser_buf_read_u32_le(&db, &width) ||
        !deser_buf_read_u32_le(&db, &height) ||
        !deser_buf_read_u32_le(&db, &channels) ||
        !deser_buf_read_u32_le(&db, &levels) ||
        !deser_buf_read_u32_le(&db, &quant_step_bits) ||
        !deser_buf_read_bytes(&db, code_lengths, DIC_SCAN_TOKEN_COUNT) ||
        !deser_buf_read_u32_le(&db, &color_transform)) {
        return DIC_HW4_FORMAT_ERROR;
    }

    if (version != DIC_BASIC_FILE_VERSION &&
        version != DIC_BASIC_FILE_INTEGER_QUANT_VERSION)
        return DIC_HW4_FORMAT_ERROR;

    quant_step =
        version == DIC_BASIC_FILE_INTEGER_QUANT_VERSION
            ? (float)quant_step_bits
            : codec_basic_float_from_bits(quant_step_bits);

    if (
        (channels != 1u && channels != 3u) || levels == 0u ||
        levels > DIC_BASIC_MAX_LEVELS || !isfinite(quant_step) ||
        quant_step <= 0.0f ||
        color_transform > 1u || (color_transform == 1u && channels != 3u)) {
        return DIC_HW4_FORMAT_ERROR;
    }

    codec_scan_set_code_lengths(code_lengths);

    encoded->width = (int)width;
    encoded->height = (int)height;
    encoded->channels = (int)channels;
    encoded->levels = (int)levels;
    encoded->quant_step = quant_step;
    encoded->color_transform = (int)color_transform;

    /* Allocate channel streams */
    encoded->channel_streams = (codec_basic_channel_stream*)calloc(
        (size_t)encoded->channels, sizeof(encoded->channel_streams[0]));
    if (encoded->channel_streams == NULL) return DIC_STATUS_MEMORY_ERROR;

    for (channel = 0; channel < encoded->channels; ++channel) {
        uint32_t num_res;
        codec_basic_channel_stream* stream = encoded->channel_streams + channel;

        if (!deser_buf_read_u32_le(&db, &num_res)) {
            codec_basic_encoded_free(encoded);
            return DIC_HW4_FORMAT_ERROR;
        }
        stream->num_resolutions = (int)num_res;
        stream->resolutions = (codec_basic_resolution_stream*)calloc(
            (size_t)num_res, sizeof(stream->resolutions[0]));
        if (stream->resolutions == NULL) {
            codec_basic_encoded_free(encoded);
            return DIC_STATUS_MEMORY_ERROR;
        }

        for (res = 0; res < (int)num_res; ++res) {
            uint32_t payload_size, bp_count;
            codec_basic_resolution_stream* rs = stream->resolutions + res;

            if (!deser_buf_read_u32_le(&db, &payload_size) ||
                !deser_buf_read_u32_le(&db, &bp_count)) {
                codec_basic_encoded_free(encoded);
                return DIC_HW4_FORMAT_ERROR;
            }
            rs->resolution = res;
            rs->num_bitplanes = (int)bp_count;

            if (bp_count > 0u) {
                int bp;
                rs->bitplanes = (codec_scan_bitplane*)calloc(
                    (size_t)bp_count, sizeof(rs->bitplanes[0]));
                if (rs->bitplanes == NULL) {
                    codec_basic_encoded_free(encoded);
                    return DIC_STATUS_MEMORY_ERROR;
                }
                for (bp = 0; bp < (int)bp_count; ++bp) {
                    status = codec_basic_deserialize_bitplane(
                        &db, rs->bitplanes + bp);
                    if (status != DIC_STATUS_OK) {
                        codec_basic_encoded_free(encoded);
                        return status;
                    }
                }
            }
        }
    }

    return DIC_STATUS_OK;
}

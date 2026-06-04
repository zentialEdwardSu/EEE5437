/**
 * @file basic_file.c
 * @brief Implements DICW v3 serialization with per-resolution payload size headers.
 *
 * v3 (this version) adds a per-resolution payload_byte_size field that allows
 * partial reads to skip unneeded resolutions via fseek without parsing bitplane
 * headers.
 */

#include "codec/basic_file.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw2/hw2_huffman.h"

/* -------------------------------------------------------------------------- */
/*  Little-endian I/O helpers                                                 */
/* -------------------------------------------------------------------------- */

static FILE *codec_basic_open_file(const char *path, const char *mode)
{
    FILE *file = NULL;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, mode) != 0) return NULL;
    return file;
#else
    return fopen(path, mode);
#endif
}

static int codec_basic_write_u32_le(FILE *file, uint32_t value)
{
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24) & 0xffu);
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes);
}

static int codec_basic_read_u32_le(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];
    if (file == NULL || value == NULL) return 0;
    if (fread(bytes, 1u, sizeof(bytes), file) != sizeof(bytes)) return 0;
    *value = (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
    return 1;
}

/* -------------------------------------------------------------------------- */
/*  Per-bitplane write/read                                                    */
/* -------------------------------------------------------------------------- */

static dic_status codec_basic_write_bitplane(FILE *file, const codec_scan_bitplane *bp)
{
    int k;

    if (!codec_basic_write_u32_le(file, (uint32_t)bp->dominant_token_count))
        return DIC_STATUS_IO_ERROR;

    for (k = 0; k < DIC_SCAN_TOKEN_COUNT; ++k)
        if (!codec_basic_write_u32_le(file, (uint32_t)bp->token_freq[k]))
            return DIC_STATUS_IO_ERROR;

    if (!codec_basic_write_u32_le(file, (uint32_t)bp->dominant_stream.bit_count))
        return DIC_STATUS_IO_ERROR;
    if (!codec_basic_write_u32_le(file, (uint32_t)bp->dominant_stream.byte_count))
        return DIC_STATUS_IO_ERROR;
    if (bp->dominant_stream.byte_count > 0u) {
        if (fwrite(bp->dominant_stream.bytes, 1u, bp->dominant_stream.byte_count, file)
            != bp->dominant_stream.byte_count)
            return DIC_STATUS_IO_ERROR;
    }

    if (!codec_basic_write_u32_le(file, (uint32_t)bp->subordinate_bit_count))
        return DIC_STATUS_IO_ERROR;
    if (!codec_basic_write_u32_le(file, (uint32_t)bp->subordinate_byte_count))
        return DIC_STATUS_IO_ERROR;
    if (bp->subordinate_byte_count > 0u) {
        if (fwrite(bp->subordinate_bits, 1u, bp->subordinate_byte_count, file)
            != bp->subordinate_byte_count)
            return DIC_STATUS_IO_ERROR;
    }

    /* End-of-bitplane sentinel */
    if (!codec_basic_write_u32_le(file, DIC_BP_END_MARKER))
        return DIC_STATUS_IO_ERROR;

    return DIC_STATUS_OK;
}

static dic_status codec_basic_read_bitplane(FILE *file, codec_scan_bitplane *bp)
{
    uint32_t u32;
    int k;

    codec_scan_bitplane_init(bp);

    if (!codec_basic_read_u32_le(file, &u32)) return DIC_HW4_FORMAT_ERROR;
    bp->dominant_token_count = (size_t)u32;

    for (k = 0; k < DIC_SCAN_TOKEN_COUNT; ++k) {
        if (!codec_basic_read_u32_le(file, &u32)) return DIC_HW4_FORMAT_ERROR;
        bp->token_freq[k] = (size_t)u32;
    }

    if (!codec_basic_read_u32_le(file, &u32)) return DIC_HW4_FORMAT_ERROR;
    bp->dominant_stream.bit_count = (size_t)u32;
    if (!codec_basic_read_u32_le(file, &u32)) return DIC_HW4_FORMAT_ERROR;
    bp->dominant_stream.byte_count = (size_t)u32;

    if (bp->dominant_stream.byte_count > 0u) {
        bp->dominant_stream.bytes = (unsigned char *)malloc(bp->dominant_stream.byte_count);
        if (bp->dominant_stream.bytes == NULL) return DIC_STATUS_MEMORY_ERROR;
        if (fread(bp->dominant_stream.bytes, 1u, bp->dominant_stream.byte_count, file)
            != bp->dominant_stream.byte_count) {
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
        bp->subordinate_bits = (unsigned char *)malloc(bp->subordinate_byte_count);
        if (bp->subordinate_bits == NULL) {
            dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
            return DIC_STATUS_MEMORY_ERROR;
        }
        if (fread(bp->subordinate_bits, 1u, bp->subordinate_byte_count, file)
            != bp->subordinate_byte_count) {
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
/*  Resolution payload size helper                                             */
/* -------------------------------------------------------------------------- */

/** Compute serialized byte size of one resolution's data (excluding the 4-byte
 *  payload_byte_size field itself). */
static size_t codec_basic_resolution_byte_size(const codec_basic_resolution_stream *rs)
{
    size_t size = 4u; /* num_bitplanes u32 */
    int bp;
    for (bp = 0; bp < rs->num_bitplanes; ++bp) {
        const codec_scan_bitplane *cur = rs->bitplanes + bp;
        size += 4u + 4u * DIC_SCAN_TOKEN_COUNT + 4u + 4u;
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

dic_status codec_basic_write_stream(FILE *file, const codec_basic_encoded_image *encoded)
{
    dic_status status = DIC_STATUS_OK;
    int channel, res, bp;

    if (file == NULL || encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (encoded->width <= 0 || encoded->height <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;
    if (encoded->channels != 1 && encoded->channels != 3)
        return DIC_HW4_INVALID_CHANNELS;
    if (encoded->levels <= 0 || encoded->quant_step <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    /* Header */
    if (fwrite(DIC_BASIC_FILE_MAGIC, 1u, 4u, file) != 4u
        || !codec_basic_write_u32_le(file, DIC_BASIC_FILE_VERSION)
        || !codec_basic_write_u32_le(file, (uint32_t)encoded->width)
        || !codec_basic_write_u32_le(file, (uint32_t)encoded->height)
        || !codec_basic_write_u32_le(file, (uint32_t)encoded->channels)
        || !codec_basic_write_u32_le(file, (uint32_t)encoded->levels)
        || !codec_basic_write_u32_le(file, (uint32_t)encoded->quant_step))
    {
        return DIC_STATUS_IO_ERROR;
    }

    for (channel = 0; channel < encoded->channels; ++channel) {
        const codec_basic_channel_stream *stream = encoded->channel_streams + channel;
        if (!codec_basic_write_u32_le(file, (uint32_t)stream->num_resolutions))
            return DIC_STATUS_IO_ERROR;
        for (res = 0; res < stream->num_resolutions; ++res) {
            const codec_basic_resolution_stream *rs = stream->resolutions + res;
            size_t payload_size = codec_basic_resolution_byte_size(rs);
            if (!codec_basic_write_u32_le(file, (uint32_t)payload_size)
                || !codec_basic_write_u32_le(file, (uint32_t)rs->num_bitplanes))
                return DIC_STATUS_IO_ERROR;
            for (bp = 0; bp < rs->num_bitplanes; ++bp) {
                status = codec_basic_write_bitplane(file, rs->bitplanes + bp);
                if (status != DIC_STATUS_OK) return status;
            }
        }
    }

    return DIC_STATUS_OK;
}

dic_status codec_basic_write_file(const char *path, const codec_basic_encoded_image *encoded)
{
    FILE *file = NULL;
    dic_status status;

    if (path == NULL || encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = codec_basic_open_file(path, "wb");
    if (file == NULL) return DIC_STATUS_IO_ERROR;

    status = codec_basic_write_stream(file, encoded);
    if (fclose(file) != 0 && status == DIC_STATUS_OK) status = DIC_STATUS_IO_ERROR;
    return status;
}

/* -------------------------------------------------------------------------- */
/*  Header read (v3)                                                          */
/* -------------------------------------------------------------------------- */

static dic_status codec_basic_read_header_v3(
    FILE *file, codec_basic_encoded_image *encoded)
{
    char magic[4];
    uint32_t version, width, height, channels, levels, quant_step;

    if (fread(magic, 1u, sizeof(magic), file) != sizeof(magic)
        || memcmp(magic, DIC_BASIC_FILE_MAGIC, sizeof(magic)) != 0
        || !codec_basic_read_u32_le(file, &version)
        || !codec_basic_read_u32_le(file, &width)
        || !codec_basic_read_u32_le(file, &height)
        || !codec_basic_read_u32_le(file, &channels)
        || !codec_basic_read_u32_le(file, &levels)
        || !codec_basic_read_u32_le(file, &quant_step))
    {
        return DIC_HW4_FORMAT_ERROR;
    }

    if (version != DIC_BASIC_FILE_VERSION
        || (channels != 1u && channels != 3u)
        || levels == 0u || quant_step == 0u)
    {
        return DIC_HW4_FORMAT_ERROR;
    }

    encoded->width = (int)width;
    encoded->height = (int)height;
    encoded->channels = (int)channels;
    encoded->levels = (int)levels;
    encoded->quant_step = (int)quant_step;
    return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Read (internal helper — resolution-limited)                                */
/* -------------------------------------------------------------------------- */

static dic_status codec_basic_read_stream_resolution_internal(
    FILE *file, int max_resolution, codec_basic_encoded_image *encoded)
{
    dic_status status;
    int channel, res;

    if (file == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    codec_basic_encoded_free(encoded);

    status = codec_basic_read_header_v3(file, encoded);
    if (status != DIC_STATUS_OK) return status;

    encoded->channel_streams = (codec_basic_channel_stream *)calloc(
        (size_t)encoded->channels, sizeof(encoded->channel_streams[0]));
    if (encoded->channel_streams == NULL) return DIC_STATUS_MEMORY_ERROR;

    for (channel = 0; channel < encoded->channels; ++channel) {
        uint32_t num_res;
        codec_basic_channel_stream *stream = encoded->channel_streams + channel;

        if (!codec_basic_read_u32_le(file, &num_res)) {
            codec_basic_encoded_free(encoded);
            return DIC_HW4_FORMAT_ERROR;
        }
        stream->num_resolutions = (int)num_res;
        stream->resolutions = (codec_basic_resolution_stream *)calloc(
            (size_t)num_res, sizeof(stream->resolutions[0]));
        if (stream->resolutions == NULL) {
            codec_basic_encoded_free(encoded);
            return DIC_STATUS_MEMORY_ERROR;
        }

        for (res = 0; res < (int)num_res; ++res) {
            uint32_t payload_size, bp_count;
            codec_basic_resolution_stream *rs = stream->resolutions + res;

            if (!codec_basic_read_u32_le(file, &payload_size)
                || !codec_basic_read_u32_le(file, &bp_count)) {
                codec_basic_encoded_free(encoded);
                return DIC_HW4_FORMAT_ERROR;
            }
            rs->resolution = res;

            if (res <= max_resolution) {
                int bp;
                rs->num_bitplanes = (int)bp_count;
                if (bp_count > 0u) {
                    rs->bitplanes = (codec_scan_bitplane *)calloc(
                        (size_t)bp_count, sizeof(rs->bitplanes[0]));
                    if (rs->bitplanes == NULL) {
                        codec_basic_encoded_free(encoded);
                        return DIC_STATUS_MEMORY_ERROR;
                    }
                    for (bp = 0; bp < (int)bp_count; ++bp) {
                        status = codec_basic_read_bitplane(file, rs->bitplanes + bp);
                        if (status != DIC_STATUS_OK) {
                            codec_basic_encoded_free(encoded);
                            return status;
                        }
                    }
                }
            } else {
                /* Skip this resolution: payload_size covers num_bitplanes (already
                 * read) + all bitplane data.  Subtract the 4 bytes we consumed. */
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

dic_status codec_basic_read_stream(FILE *file, codec_basic_encoded_image *encoded)
{
    return codec_basic_read_stream_resolution_internal(file, INT_MAX, encoded);
}

dic_status codec_basic_read_stream_resolution(
    FILE *file, int max_resolution, codec_basic_encoded_image *encoded)
{
    return codec_basic_read_stream_resolution_internal(file, max_resolution, encoded);
}

dic_status codec_basic_read_file(const char *path, codec_basic_encoded_image *encoded)
{
    FILE *file = NULL;
    dic_status status;

    if (path == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = codec_basic_open_file(path, "rb");
    if (file == NULL) return DIC_STATUS_FILE_OPEN_ERROR;

    status = codec_basic_read_stream(file, encoded);
    fclose(file);
    if (status != DIC_STATUS_OK) codec_basic_encoded_free(encoded);
    return status;
}

dic_status codec_basic_read_file_resolution(
    const char *path, int max_resolution, codec_basic_encoded_image *encoded)
{
    FILE *file = NULL;
    dic_status status;

    if (path == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = codec_basic_open_file(path, "rb");
    if (file == NULL) return DIC_STATUS_FILE_OPEN_ERROR;

    status = codec_basic_read_stream_resolution(file, max_resolution, encoded);
    fclose(file);
    if (status != DIC_STATUS_OK) codec_basic_encoded_free(encoded);
    return status;
}

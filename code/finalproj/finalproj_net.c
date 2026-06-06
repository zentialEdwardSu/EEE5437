/**
 * @file finalproj_net.c
 * @brief File-backed, quality-progressive network transport.
 */

#include "finalproj/finalproj_net.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "codec/metrics.h"
#include "errors/errors.h"
#include "finalproj/finalproj_codec.h"
#include "image_u8/image_u8.h"
#include "net/net.h"
#include "net/net_platform.h"
#include "ppm/ppm.h"

#define FINALPROJ_IO_CHUNK_SIZE 8192u
#define FINALPROJ_MAX_PAYLOAD_SIZE (256u * 1024u * 1024u)
#define FINALPROJ_MAX_BITPLANES 32u
#define FINALPROJ_QUALITY_HEADER_SIZE 40u
#define FINALPROJ_RECEIVE_MAX_ATTEMPTS 10000
#define FINALPROJ_QUALITY_MAGIC "DICQ"
#define FINALPROJ_RESOLUTION_MAGIC "DICR"
#define FINALPROJ_QUALITY_VERSION 2u
#define FINALPROJ_INTEGER_QUANT_VERSION 1u
#define FINALPROJ_LAYER_END_MARKER 0xFFFFFFFEu

typedef struct receive_parser {
    size_t position;
    int header_parsed;
    int finished;
    int layer;
    int channel;
    int resolution;
    int bitplane;
    int max_bitplanes;
} receive_parser;

static void write_u32_le_bytes(unsigned char bytes[4], uint32_t value) {
    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24) & 0xffu);
}

static uint32_t read_u32_le_bytes(const unsigned char bytes[4]) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float float_from_bits(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int write_u32_le(FILE* file, uint32_t value) {
    unsigned char bytes[4];
    write_u32_le_bytes(bytes, value);
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes);
}

static FILE* open_temporary_file(void) {
#if defined(_WIN32)
    FILE* file = NULL;
    if (tmpfile_s(&file) != 0) return NULL;
    return file;
#else
    return tmpfile();
#endif
}

static int add_size_checked(size_t left, size_t right, size_t* result) {
    if (left > SIZE_MAX - right) return 0;
    *result = left + right;
    return 1;
}

static int file_seek_offset(FILE* file, size_t offset) {
    if (file == NULL || offset > (size_t)LONG_MAX) return 0;
    return fseek(file, (long)offset, SEEK_SET) == 0;
}

static int file_read_at(FILE* file, size_t offset, void* data, size_t size) {
    if (!file_seek_offset(file, offset)) return 0;
    return size == 0u || fread(data, 1u, size, file) == size;
}

static int file_read_u32_at(FILE* file, size_t offset, uint32_t* value) {
    unsigned char bytes[4];
    if (value == NULL || !file_read_at(file, offset, bytes, sizeof(bytes)))
        return 0;
    *value = read_u32_le_bytes(bytes);
    return 1;
}

static dic_status net_receive_all(net_control* net, uint8_t* buffer,
                                  size_t total) {
    size_t received_total = 0u;
    int attempts = 0;

    while (received_total < total) {
        size_t received = 0u;
        dic_status status = net_receive(net, buffer + received_total,
                                        total - received_total, &received);
        if (status != DIC_STATUS_OK) return status;
        if (received > 0u) {
            received_total += received;
            attempts = 0;
        } else {
            if (++attempts >= FINALPROJ_RECEIVE_MAX_ATTEMPTS)
                return DIC_NET_RECEIVE_ERROR;
            net_platform_sleep_ms(10u);
        }
    }
    return DIC_STATUS_OK;
}

static int encoded_max_bitplanes(const codec_basic_encoded_image* encoded) {
    int maximum = 0;
    int channel, resolution;

    for (channel = 0; channel < encoded->channels; ++channel) {
        const codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        for (resolution = 0; resolution < stream->num_resolutions;
             ++resolution) {
            int count = stream->resolutions[resolution].num_bitplanes;
            if (count > maximum) maximum = count;
        }
    }
    return maximum;
}

static dic_status replace_file(const char* source, const char* destination) {
#if defined(_WIN32)
    if (!MoveFileExA(source, destination,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return DIC_STATUS_IO_ERROR;
#else
    if (rename(source, destination) != 0) return DIC_STATUS_IO_ERROR;
#endif
    return DIC_STATUS_OK;
}

static dic_status write_ppm_atomic(const char* path,
                                   const dic_image_u8* image) {
    char temporary_path[1024];
    int length =
        snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path);
    dic_status status;

    if (length < 0 || (size_t)length >= sizeof(temporary_path))
        return DIC_STATUS_IO_ERROR;
    status = dic_ppm_write(temporary_path, image);
    if (status != DIC_STATUS_OK) {
        remove(temporary_path);
        return status;
    }
    status = replace_file(temporary_path, path);
    if (status != DIC_STATUS_OK) remove(temporary_path);
    return status;
}

static dic_status write_quality_bitplane(FILE* file,
                                         const codec_scan_bitplane* bitplane) {
    if (bitplane->dominant_token_count > UINT32_MAX ||
        bitplane->dominant_stream.bit_count > UINT32_MAX ||
        bitplane->dominant_stream.byte_count > UINT32_MAX ||
        bitplane->subordinate_bit_count > UINT32_MAX ||
        bitplane->subordinate_byte_count > UINT32_MAX)
        return DIC_STATUS_INVALID_ARGUMENT;

    if (!write_u32_le(file, (uint32_t)bitplane->dominant_token_count) ||
        !write_u32_le(file, (uint32_t)bitplane->dominant_stream.bit_count) ||
        !write_u32_le(file, (uint32_t)bitplane->dominant_stream.byte_count) ||
        (bitplane->dominant_stream.byte_count > 0u &&
         fwrite(bitplane->dominant_stream.bytes, 1u,
                bitplane->dominant_stream.byte_count,
                file) != bitplane->dominant_stream.byte_count) ||
        !write_u32_le(file, (uint32_t)bitplane->subordinate_bit_count) ||
        !write_u32_le(file, (uint32_t)bitplane->subordinate_byte_count) ||
        (bitplane->subordinate_byte_count > 0u &&
         fwrite(bitplane->subordinate_bits, 1u,
                bitplane->subordinate_byte_count,
                file) != bitplane->subordinate_byte_count) ||
        !write_u32_le(file, DIC_BP_END_MARKER))
        return DIC_STATUS_IO_ERROR;
    return DIC_STATUS_OK;
}

/**
 * Network payload format: fixed metadata, all per-resolution bitplane counts,
 * then bitplanes in quality-layer order. DICW v6 file serialization is
 * unchanged; only the network ordering uses DICQ.
 */
static dic_status write_quality_stream(
    FILE* file, const codec_basic_encoded_image* encoded) {
    unsigned char code_lengths[DIC_SCAN_TOKEN_COUNT];
    int max_bitplanes = encoded_max_bitplanes(encoded);
    int channel, resolution, layer;

    if (file == NULL || encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    codec_scan_get_code_lengths(code_lengths);
    if (fwrite(FINALPROJ_QUALITY_MAGIC, 1u, 4u, file) != 4u ||
        !write_u32_le(file, FINALPROJ_QUALITY_VERSION) ||
        !write_u32_le(file, (uint32_t)encoded->width) ||
        !write_u32_le(file, (uint32_t)encoded->height) ||
        !write_u32_le(file, (uint32_t)encoded->channels) ||
        !write_u32_le(file, (uint32_t)encoded->levels) ||
        !write_u32_le(file, float_bits(encoded->quant_step)) ||
        fwrite(code_lengths, 1u, DIC_SCAN_TOKEN_COUNT, file) !=
            DIC_SCAN_TOKEN_COUNT ||
        !write_u32_le(file, (uint32_t)encoded->color_transform) ||
        !write_u32_le(file, (uint32_t)max_bitplanes))
        return DIC_STATUS_IO_ERROR;

    for (channel = 0; channel < encoded->channels; ++channel) {
        const codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        for (resolution = 0; resolution <= encoded->levels; ++resolution) {
            int count = stream->resolutions[resolution].num_bitplanes;
            if (count < 0 || count > (int)FINALPROJ_MAX_BITPLANES ||
                !write_u32_le(file, (uint32_t)count))
                return DIC_STATUS_IO_ERROR;
        }
    }

    for (layer = 0; layer < max_bitplanes; ++layer) {
        for (channel = 0; channel < encoded->channels; ++channel) {
            const codec_basic_channel_stream* stream =
                encoded->channel_streams + channel;
            for (resolution = 0; resolution <= encoded->levels; ++resolution) {
                const codec_basic_resolution_stream* current =
                    stream->resolutions + resolution;
                dic_status status;
                if (layer >= current->num_bitplanes) continue;
                status =
                    write_quality_bitplane(file, current->bitplanes + layer);
                if (status != DIC_STATUS_OK) return status;
            }
        }
        if (!write_u32_le(file, FINALPROJ_LAYER_END_MARKER))
            return DIC_STATUS_IO_ERROR;
    }
    return DIC_STATUS_OK;
}

static dic_status write_resolution_stream(
    FILE* file, const codec_basic_encoded_image* encoded) {
    unsigned char code_lengths[DIC_SCAN_TOKEN_COUNT];
    int channel, resolution, bitplane;

    if (file == NULL || encoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    codec_scan_get_code_lengths(code_lengths);
    if (fwrite(FINALPROJ_RESOLUTION_MAGIC, 1u, 4u, file) != 4u ||
        !write_u32_le(file, FINALPROJ_QUALITY_VERSION) ||
        !write_u32_le(file, (uint32_t)encoded->width) ||
        !write_u32_le(file, (uint32_t)encoded->height) ||
        !write_u32_le(file, (uint32_t)encoded->channels) ||
        !write_u32_le(file, (uint32_t)encoded->levels) ||
        !write_u32_le(file, float_bits(encoded->quant_step)) ||
        fwrite(code_lengths, 1u, DIC_SCAN_TOKEN_COUNT, file) !=
            DIC_SCAN_TOKEN_COUNT ||
        !write_u32_le(file, (uint32_t)encoded->color_transform) ||
        !write_u32_le(file, (uint32_t)(encoded->levels + 1)))
        return DIC_STATUS_IO_ERROR;

    for (channel = 0; channel < encoded->channels; ++channel) {
        const codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        for (resolution = 0; resolution <= encoded->levels; ++resolution) {
            int count = stream->resolutions[resolution].num_bitplanes;
            if (count < 0 || count > (int)FINALPROJ_MAX_BITPLANES ||
                !write_u32_le(file, (uint32_t)count))
                return DIC_STATUS_IO_ERROR;
        }
    }

    for (resolution = 0; resolution <= encoded->levels; ++resolution) {
        for (channel = 0; channel < encoded->channels; ++channel) {
            const codec_basic_resolution_stream* current =
                encoded->channel_streams[channel].resolutions + resolution;
            for (bitplane = 0; bitplane < current->num_bitplanes; ++bitplane) {
                dic_status status =
                    write_quality_bitplane(file, current->bitplanes + bitplane);
                if (status != DIC_STATUS_OK) return status;
            }
        }
        if (!write_u32_le(file, FINALPROJ_LAYER_END_MARKER))
            return DIC_STATUS_IO_ERROR;
    }
    return DIC_STATUS_OK;
}

static void free_partial_bitplane(codec_scan_bitplane* bitplane) {
    codec_scan_bitplane_free(bitplane);
    codec_scan_bitplane_init(bitplane);
}

static dic_status try_load_bitplane(FILE* file, size_t available,
                                    size_t position, size_t maximum_token_count,
                                    codec_scan_bitplane* target,
                                    size_t* next_position, int* complete) {
    uint32_t token_count, dominant_bits, dominant_bytes;
    uint32_t subordinate_bits, subordinate_bytes, marker;
    size_t dominant_start, subordinate_header, subordinate_start, marker_pos;
    size_t end;

    *complete = 0;
    if (!add_size_checked(position, 12u, &dominant_start) ||
        dominant_start > available)
        return DIC_STATUS_OK;
    if (!file_read_u32_at(file, position, &token_count) ||
        !file_read_u32_at(file, position + 4u, &dominant_bits) ||
        !file_read_u32_at(file, position + 8u, &dominant_bytes))
        return DIC_STATUS_FILE_READ_ERROR;

    if ((size_t)token_count > maximum_token_count ||
        (uint64_t)dominant_bits > (uint64_t)dominant_bytes * 8u ||
        !add_size_checked(dominant_start, (size_t)dominant_bytes,
                          &subordinate_header))
        return DIC_HW4_FORMAT_ERROR;
    if (!add_size_checked(subordinate_header, 8u, &subordinate_start) ||
        subordinate_start > available)
        return DIC_STATUS_OK;
    if (!file_read_u32_at(file, subordinate_header, &subordinate_bits) ||
        !file_read_u32_at(file, subordinate_header + 4u, &subordinate_bytes))
        return DIC_STATUS_FILE_READ_ERROR;

    if ((uint64_t)subordinate_bits > (uint64_t)subordinate_bytes * 8u ||
        !add_size_checked(subordinate_start, (size_t)subordinate_bytes,
                          &marker_pos) ||
        !add_size_checked(marker_pos, 4u, &end))
        return DIC_HW4_FORMAT_ERROR;
    if (end > available) return DIC_STATUS_OK;
    if (!file_read_u32_at(file, marker_pos, &marker) ||
        marker != DIC_BP_END_MARKER)
        return DIC_HW4_FORMAT_ERROR;

    codec_scan_bitplane_init(target);
    target->dominant_token_count = (size_t)token_count;
    target->dominant_stream.bit_count = (size_t)dominant_bits;
    target->dominant_stream.byte_count = (size_t)dominant_bytes;
    target->subordinate_bit_count = (size_t)subordinate_bits;
    target->subordinate_byte_count = (size_t)subordinate_bytes;

    if (dominant_bytes > 0u) {
        target->dominant_stream.bytes =
            (unsigned char*)malloc((size_t)dominant_bytes);
        if (target->dominant_stream.bytes == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        if (!file_read_at(file, dominant_start, target->dominant_stream.bytes,
                          (size_t)dominant_bytes)) {
            free_partial_bitplane(target);
            return DIC_STATUS_FILE_READ_ERROR;
        }
    }
    if (subordinate_bytes > 0u) {
        target->subordinate_bits =
            (unsigned char*)malloc((size_t)subordinate_bytes);
        if (target->subordinate_bits == NULL) {
            free_partial_bitplane(target);
            return DIC_STATUS_MEMORY_ERROR;
        }
        if (!file_read_at(file, subordinate_start, target->subordinate_bits,
                          (size_t)subordinate_bytes)) {
            free_partial_bitplane(target);
            return DIC_STATUS_FILE_READ_ERROR;
        }
    }

    *next_position = end;
    *complete = 1;
    return DIC_STATUS_OK;
}

static dic_status parse_progressive_header(FILE* file, size_t available,
                                           float expected_quant,
                                           finalproj_scaling_mode scaling_mode,
                                           codec_basic_encoded_image* encoded,
                                           receive_parser* parser) {
    unsigned char header[FINALPROJ_QUALITY_HEADER_SIZE];
    unsigned char code_lengths[DIC_SCAN_TOKEN_COUNT];
    uint32_t version, width, height, channels, levels, quant_bits, transform;
    uint32_t max_bitplanes;
    float quant;
    size_t count_bytes, header_end;
    int channel, resolution;

    if (available < sizeof(header)) return DIC_STATUS_OK;
    if (!file_read_at(file, 0u, header, sizeof(header)))
        return DIC_STATUS_FILE_READ_ERROR;

    version = read_u32_le_bytes(header + 4u);
    width = read_u32_le_bytes(header + 8u);
    height = read_u32_le_bytes(header + 12u);
    channels = read_u32_le_bytes(header + 16u);
    levels = read_u32_le_bytes(header + 20u);
    quant_bits = read_u32_le_bytes(header + 24u);
    memcpy(code_lengths, header + 28u, sizeof(code_lengths));
    transform = read_u32_le_bytes(header + 32u);
    max_bitplanes = read_u32_le_bytes(header + 36u);

    const char* expected_magic = scaling_mode == FINALPROJ_SCALING_SNR
                                     ? FINALPROJ_QUALITY_MAGIC
                                     : FINALPROJ_RESOLUTION_MAGIC;

    if (version != FINALPROJ_QUALITY_VERSION &&
        version != FINALPROJ_INTEGER_QUANT_VERSION)
        return DIC_HW4_FORMAT_ERROR;
    quant = version == FINALPROJ_INTEGER_QUANT_VERSION
                ? (float)quant_bits
                : float_from_bits(quant_bits);

    if (memcmp(header, expected_magic, 4u) != 0 || width == 0u ||
        width > (uint32_t)INT_MAX || height == 0u ||
        height > (uint32_t)INT_MAX || (channels != 1u && channels != 3u) ||
        levels == 0u || levels > DIC_BASIC_MAX_LEVELS || !isfinite(quant) ||
        quant <= 0.0f || transform > 1u ||
        (transform == 1u && channels != 3u) ||
        (scaling_mode == FINALPROJ_SCALING_SNR &&
         max_bitplanes > FINALPROJ_MAX_BITPLANES) ||
        (scaling_mode == FINALPROJ_SCALING_RESOLUTION &&
         max_bitplanes != levels + 1u))
        return DIC_HW4_FORMAT_ERROR;

    count_bytes = (size_t)channels * ((size_t)levels + 1u) * 4u;
    if (!add_size_checked(sizeof(header), count_bytes, &header_end))
        return DIC_HW4_FORMAT_ERROR;
    if (available < header_end) return DIC_STATUS_OK;

    codec_scan_set_code_lengths(code_lengths);
    {
        dic_status status = codec_basic_encoded_alloc_streams(
            encoded, (int)width, (int)height, (int)channels, (int)levels,
            quant, (int)transform);
        if (status != DIC_STATUS_OK) return status;
    }

    parser->position = sizeof(header);
    for (channel = 0; channel < encoded->channels; ++channel) {
        codec_basic_channel_stream* stream = encoded->channel_streams + channel;
        for (resolution = 0; resolution <= encoded->levels; ++resolution) {
            codec_basic_resolution_stream* current =
                stream->resolutions + resolution;
            uint32_t count;
            int index;
            if (!file_read_u32_at(file, parser->position, &count) ||
                count > FINALPROJ_MAX_BITPLANES) {
                codec_basic_encoded_free(encoded);
                return DIC_HW4_FORMAT_ERROR;
            }
            parser->position += 4u;
            current->num_bitplanes = (int)count;
            if (count == 0u) continue;
            current->bitplanes = (codec_scan_bitplane*)calloc(
                (size_t)count, sizeof(current->bitplanes[0]));
            if (current->bitplanes == NULL) {
                codec_basic_encoded_free(encoded);
                return DIC_STATUS_MEMORY_ERROR;
            }
            for (index = 0; index < (int)count; ++index)
                codec_scan_bitplane_init(current->bitplanes + index);
        }
    }

    parser->header_parsed = 1;
    parser->max_bitplanes = (int)max_bitplanes;
    parser->finished = max_bitplanes == 0u;
    parser->layer = 0;
    parser->channel = 0;
    parser->resolution = 0;
    parser->bitplane = 0;

    if (quant != expected_quant)
        fprintf(stderr,
                "warning: received quant_step=%.9g but expected %.9g; using "
                "received value\n",
                quant, expected_quant);
    printf("Image: %dx%d, %d channels, %d levels, quant=%.9g, %s layers=%d\n",
           encoded->width, encoded->height, encoded->channels, encoded->levels,
           encoded->quant_step,
           scaling_mode == FINALPROJ_SCALING_SNR ? "quality" : "resolution",
           parser->max_bitplanes);
    fflush(stdout);
    return DIC_STATUS_OK;
}

/**
 * Parse records until one complete quality layer is available. The decoder
 * only sees a layer after every channel and resolution has received that
 * layer's bitplane (or declared that it has no such bitplane).
 */
static dic_status parse_next_quality_layer(FILE* file, size_t available,
                                           float expected_quant,
                                           codec_basic_encoded_image* encoded,
                                           receive_parser* parser,
                                           int* layer_completed) {
    dic_status status;

    *layer_completed = 0;
    if (!parser->header_parsed) {
        status =
            parse_progressive_header(file, available, expected_quant,
                                     FINALPROJ_SCALING_SNR, encoded, parser);
        if (status != DIC_STATUS_OK || !parser->header_parsed) return status;
    }
    if (parser->finished) return DIC_STATUS_OK;

    while (parser->channel < encoded->channels) {
        codec_basic_channel_stream* stream =
            encoded->channel_streams + parser->channel;
        while (parser->resolution <= encoded->levels) {
            codec_basic_resolution_stream* current =
                stream->resolutions + parser->resolution;
            if (parser->layer < current->num_bitplanes) {
                int complete = 0;
                size_t next_position = parser->position;
                status = try_load_bitplane(
                    file, available, parser->position,
                    (size_t)encoded->width * (size_t)encoded->height,
                    current->bitplanes + parser->layer, &next_position,
                    &complete);
                if (status != DIC_STATUS_OK || !complete) return status;
                parser->position = next_position;
            }
            parser->resolution++;
        }
        parser->channel++;
        parser->resolution = 0;
    }

    if (parser->position + 4u > available) return DIC_STATUS_OK;
    {
        uint32_t marker;
        if (!file_read_u32_at(file, parser->position, &marker))
            return DIC_STATUS_FILE_READ_ERROR;
        if (marker != FINALPROJ_LAYER_END_MARKER) return DIC_HW4_FORMAT_ERROR;
    }
    parser->position += 4u;
    parser->layer++;
    parser->channel = 0;
    parser->resolution = 0;
    parser->finished = parser->layer >= parser->max_bitplanes;
    *layer_completed = 1;
    return DIC_STATUS_OK;
}

static dic_status parse_next_resolution_layer(
    FILE* file, size_t available, float expected_quant,
    codec_basic_encoded_image* encoded, receive_parser* parser,
    int* layer_completed) {
    dic_status status;

    *layer_completed = 0;
    if (!parser->header_parsed) {
        status = parse_progressive_header(file, available, expected_quant,
                                          FINALPROJ_SCALING_RESOLUTION, encoded,
                                          parser);
        if (status != DIC_STATUS_OK || !parser->header_parsed) return status;
    }
    if (parser->finished) return DIC_STATUS_OK;

    while (parser->channel < encoded->channels) {
        codec_basic_resolution_stream* current =
            encoded->channel_streams[parser->channel].resolutions +
            parser->layer;
        while (parser->bitplane < current->num_bitplanes) {
            int complete = 0;
            size_t next_position = parser->position;
            status = try_load_bitplane(
                file, available, parser->position,
                (size_t)encoded->width * (size_t)encoded->height,
                current->bitplanes + parser->bitplane, &next_position,
                &complete);
            if (status != DIC_STATUS_OK || !complete) return status;
            parser->position = next_position;
            parser->bitplane++;
        }
        parser->channel++;
        parser->bitplane = 0;
    }

    if (parser->position + 4u > available) return DIC_STATUS_OK;
    {
        uint32_t marker;
        if (!file_read_u32_at(file, parser->position, &marker))
            return DIC_STATUS_FILE_READ_ERROR;
        if (marker != FINALPROJ_LAYER_END_MARKER) return DIC_HW4_FORMAT_ERROR;
    }
    parser->position += 4u;
    parser->layer++;
    parser->channel = 0;
    parser->bitplane = 0;
    parser->finished = parser->layer >= parser->max_bitplanes;
    *layer_completed = 1;
    return DIC_STATUS_OK;
}

static dic_status decode_and_publish_quality(
    const codec_basic_encoded_image* encoded, int bitplanes,
    const char* output_file, const dic_image_u8* original, int has_original,
    dic_image_u8* decoded) {
    dic_status status;

    dic_image_u8_free(decoded);
    status =
        codec_basic_decode_image(encoded, encoded->levels, bitplanes, decoded);
    if (status != DIC_STATUS_OK) return status;
    status = write_ppm_atomic(output_file, decoded);
    if (status != DIC_STATUS_OK) return status;

    if (has_original && original->width == decoded->width &&
        original->height == decoded->height &&
        original->channels == decoded->channels) {
        double psnr = codec_metric_psnr_u8(
            original->data, decoded->data,
            dic_image_u8_sample_count(decoded->width, decoded->height,
                                      decoded->channels));
        printf("Quality layer %d: %dx%d, PSNR = %.1f dB\n", bitplanes,
               decoded->width, decoded->height, psnr);
    } else {
        printf("Quality layer %d: %dx%d\n", bitplanes, decoded->width,
               decoded->height);
    }
    fflush(stdout);
    return DIC_STATUS_OK;
}

static dic_status decode_and_publish_resolution(
    const codec_basic_encoded_image* encoded, int resolution,
    const char* output_file, const dic_image_u8* original, int has_original,
    dic_image_u8* decoded) {
    dic_status status;

    dic_image_u8_free(decoded);
    status = codec_basic_decode_image(encoded, resolution, 0, decoded);
    if (status != DIC_STATUS_OK) return status;
    status = write_ppm_atomic(output_file, decoded);
    if (status != DIC_STATUS_OK) return status;

    if (has_original && original->width == decoded->width &&
        original->height == decoded->height &&
        original->channels == decoded->channels) {
        double psnr = codec_metric_psnr_u8(
            original->data, decoded->data,
            dic_image_u8_sample_count(decoded->width, decoded->height,
                                      decoded->channels));
        printf("Resolution layer %d: %dx%d, PSNR = %.1f dB\n", resolution,
               decoded->width, decoded->height, psnr);
    } else {
        printf("Resolution layer %d: %dx%d\n", resolution, decoded->width,
               decoded->height);
    }
    fflush(stdout);
    return DIC_STATUS_OK;
}

static void print_transfer_progress(const char* label, size_t completed,
                                    size_t total, int* last_percent) {
    int percent;
    if (total == 0u || last_percent == NULL) return;
    percent = (int)(completed * 100u / total);
    if (percent == *last_percent) return;
    printf("%s %3d%% (%zu/%zu bytes)\n", label, percent, completed, total);
    fflush(stdout);
    *last_percent = percent;
}

int networkSend(const char* inputFile, const char* host, int port, float quant,
                uint32_t rateLimit, finalproj_scaling_mode scalingMode) {
    dic_image_u8 image;
    codec_basic_encoded_image encoded;
    net_config config;
    net_control* net = NULL;
    FILE* bitstream = NULL;
    dic_status status;
    uint8_t header[4];
    uint8_t io_buffer[FINALPROJ_IO_CHUNK_SIZE];
    size_t payload_size = 0u;
    size_t sent_total = 0u;
    int last_percent = -1;
    int image_width = 0;
    int image_height = 0;
    int ok = 0;

    dic_image_u8_init(&image);
    codec_basic_encoded_init(&encoded);

    status = dic_ppm_read(inputFile, &image);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to read input image: %s\n",
                dic_status_message(status));
        goto cleanup;
    }
    image_width = image.width;
    image_height = image.height;
    printf("Input: %s (%dx%d, %d channel(s))\n", inputFile, image.width,
           image.height, image.channels);
    fflush(stdout);

    status = codec_basic_encode_image(image.data, image.width, image.height,
                                      image.channels, FINALPROJ_LEVELS, quant,
                                      0, &encoded);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: encode failed: %s\n",
                dic_status_message(status));
        goto cleanup;
    }

    bitstream = open_temporary_file();
    if (bitstream == NULL) {
        fprintf(stderr, "error: failed to create temporary bitstream\n");
        goto cleanup;
    }
    status = scalingMode == FINALPROJ_SCALING_SNR
                 ? write_quality_stream(bitstream, &encoded)
                 : write_resolution_stream(bitstream, &encoded);
    if (status != DIC_STATUS_OK || fflush(bitstream) != 0 ||
        fseek(bitstream, 0L, SEEK_END) != 0) {
        fprintf(stderr, "error: failed to write progressive stream: %s\n",
                dic_status_message(status));
        goto cleanup;
    }
    {
        long end = ftell(bitstream);
        if (end <= 0L || (unsigned long)end > FINALPROJ_MAX_PAYLOAD_SIZE) {
            fprintf(stderr, "error: invalid serialized size\n");
            goto cleanup;
        }
        payload_size = (size_t)end;
    }
    if (fseek(bitstream, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "error: failed to rewind temporary bitstream\n");
        goto cleanup;
    }

    printf("%s stream: %zu bytes, %d layers (%.4f bpp)\n",
           scalingMode == FINALPROJ_SCALING_SNR ? "SNR" : "Resolution",
           payload_size,
           scalingMode == FINALPROJ_SCALING_SNR
               ? encoded_max_bitplanes(&encoded)
               : encoded.levels + 1,
           (double)(payload_size * 8u) /
               (double)((size_t)image_width * (size_t)image_height));
    fflush(stdout);

    codec_basic_encoded_free(&encoded);
    dic_image_u8_free(&image);

    net_config_init(&config);
    config.transport = net_TRANSPORT_TCP;
    config.host = host;
    config.peer_port = (uint16_t)port;
    config.buffer_capacity = 256u * 1024u;
    config.bytes_per_second = rateLimit;

    printf("Connecting to %s:%d...\n", host, port);
    fflush(stdout);
    status = net_control_open(&net, &config);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to connect: %s\n",
                dic_status_message(status));
        goto cleanup;
    }

    write_u32_le_bytes(header, (uint32_t)payload_size);
    {
        size_t sent = 0u;
        status = net_send(net, header, sizeof(header), &sent);
        if (status != DIC_STATUS_OK || sent != sizeof(header)) {
            fprintf(stderr, "error: failed to send payload header\n");
            goto cleanup;
        }
    }

    printf("Sending %zu bytes%s...\n", payload_size,
           rateLimit > 0u ? " with rate limit" : "");
    fflush(stdout);
    while (sent_total < payload_size) {
        size_t wanted = payload_size - sent_total;
        size_t read_count;
        size_t sent = 0u;
        if (wanted > sizeof(io_buffer)) wanted = sizeof(io_buffer);

        read_count = fread(io_buffer, 1u, wanted, bitstream);
        if (read_count != wanted) {
            fprintf(stderr, "error: failed to read temporary bitstream\n");
            goto cleanup;
        }
        status = net_send(net, io_buffer, read_count, &sent);
        if (status != DIC_STATUS_OK || sent != read_count) {
            fprintf(stderr, "error: failed to send payload: %s\n",
                    dic_status_message(status));
            goto cleanup;
        }
        sent_total += sent;
        print_transfer_progress("Sent", sent_total, payload_size,
                                &last_percent);
    }

    printf("Transfer complete: %zu bytes\n", payload_size);
    ok = 1;

cleanup:
    if (net != NULL) net_control_close(net);
    if (bitstream != NULL) fclose(bitstream);
    codec_basic_encoded_free(&encoded);
    dic_image_u8_free(&image);
    return ok;
}

int networkReceive(int port, const char* outputFile, float quant,
                   const char* originalFile,
                   finalproj_scaling_mode scalingMode) {
    net_config config;
    net_control* net = NULL;
    FILE* bitstream = NULL;
    codec_basic_encoded_image encoded;
    dic_image_u8 original;
    dic_image_u8 decoded;
    receive_parser parser;
    dic_status status;
    uint8_t header[4];
    uint8_t io_buffer[FINALPROJ_IO_CHUNK_SIZE];
    uint32_t payload_size;
    size_t received_total = 0u;
    int attempts = 0;
    int last_percent = -1;
    int published_layers = 0;
    int has_original = 0;
    int ok = 0;
    time_t receive_start;

    memset(&parser, 0, sizeof(parser));
    codec_basic_encoded_init(&encoded);
    dic_image_u8_init(&original);
    dic_image_u8_init(&decoded);

    net_config_init(&config);
    config.transport = net_TRANSPORT_TCP;
    config.bind_port = (uint16_t)port;
    config.peer_port = 0u;
    config.buffer_capacity = 256u * 1024u;

    status = net_control_open(&net, &config);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to listen on port %d: %s\n", port,
                dic_status_message(status));
        goto cleanup;
    }
    printf("Listening on port %d...\n", (int)net_control_port(net));
    fflush(stdout);

    status = net_receive_all(net, header, sizeof(header));
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to receive header: %s\n",
                dic_status_message(status));
        goto cleanup;
    }
    payload_size = read_u32_le_bytes(header);
    if (payload_size == 0u || payload_size > FINALPROJ_MAX_PAYLOAD_SIZE) {
        fprintf(stderr, "error: invalid payload size: %u\n", payload_size);
        goto cleanup;
    }

    bitstream = open_temporary_file();
    if (bitstream == NULL) {
        fprintf(stderr, "error: failed to create receive tempfile\n");
        goto cleanup;
    }

    if (originalFile != NULL) {
        status = dic_ppm_read(originalFile, &original);
        if (status == DIC_STATUS_OK)
            has_original = 1;
        else
            fprintf(stderr, "warning: could not read original image '%s'\n",
                    originalFile);
    }

    {
        const char* peer = net_control_peer_name(net);
        printf("Connection accepted%s%s, receiving %u bytes...\n",
               peer != NULL ? " from " : "", peer != NULL ? peer : "",
               payload_size);
        fflush(stdout);
    }

    receive_start = time(NULL);
    while (received_total < (size_t)payload_size) {
        size_t capacity = (size_t)payload_size - received_total;
        size_t received = 0u;
        if (capacity > sizeof(io_buffer)) capacity = sizeof(io_buffer);

        status = net_receive(net, io_buffer, capacity, &received);
        if (status != DIC_STATUS_OK) {
            fprintf(stderr, "error: failed to receive payload: %s\n",
                    dic_status_message(status));
            goto cleanup;
        }
        if (received == 0u) {
            if (++attempts >= FINALPROJ_RECEIVE_MAX_ATTEMPTS) {
                fprintf(stderr, "error: receive timed out\n");
                goto cleanup;
            }
            net_platform_sleep_ms(10u);
            continue;
        }
        attempts = 0;

        if (fseek(bitstream, 0L, SEEK_END) != 0 ||
            fwrite(io_buffer, 1u, received, bitstream) != received ||
            fflush(bitstream) != 0) {
            fprintf(stderr, "error: failed to append receive tempfile\n");
            goto cleanup;
        }
        received_total += received;
        print_transfer_progress("Received", received_total,
                                (size_t)payload_size, &last_percent);

        while (1) {
            int layer_completed = 0;
            status = scalingMode == FINALPROJ_SCALING_SNR
                         ? parse_next_quality_layer(bitstream, received_total,
                                                    quant, &encoded, &parser,
                                                    &layer_completed)
                         : parse_next_resolution_layer(
                               bitstream, received_total, quant, &encoded,
                               &parser, &layer_completed);
            if (status != DIC_STATUS_OK) {
                fprintf(stderr, "error: invalid progressive stream: %s\n",
                        dic_status_message(status));
                goto cleanup;
            }
            if (!layer_completed) break;

            published_layers = parser.layer;
            status = scalingMode == FINALPROJ_SCALING_SNR
                         ? decode_and_publish_quality(
                               &encoded, published_layers, outputFile,
                               &original, has_original, &decoded)
                         : decode_and_publish_resolution(
                               &encoded, published_layers - 1, outputFile,
                               &original, has_original, &decoded);
            if (status != DIC_STATUS_OK) {
                fprintf(stderr, "error: progressive decode/write failed: %s\n",
                        dic_status_message(status));
                goto cleanup;
            }
        }
    }

    while (!parser.finished) {
        int layer_completed = 0;
        status =
            scalingMode == FINALPROJ_SCALING_SNR
                ? parse_next_quality_layer(bitstream, received_total, quant,
                                           &encoded, &parser, &layer_completed)
                : parse_next_resolution_layer(bitstream, received_total, quant,
                                              &encoded, &parser,
                                              &layer_completed);
        if (status != DIC_STATUS_OK || !layer_completed) {
            fprintf(stderr, "error: truncated or invalid progressive stream\n");
            goto cleanup;
        }
        published_layers = parser.layer;
        status = scalingMode == FINALPROJ_SCALING_SNR
                     ? decode_and_publish_quality(&encoded, published_layers,
                                                  outputFile, &original,
                                                  has_original, &decoded)
                     : decode_and_publish_resolution(
                           &encoded, published_layers - 1, outputFile,
                           &original, has_original, &decoded);
        if (status != DIC_STATUS_OK) goto cleanup;
    }
    if (parser.position != (size_t)payload_size) {
        fprintf(stderr, "error: trailing bytes in progressive stream\n");
        goto cleanup;
    }

    if (published_layers == 0 && scalingMode == FINALPROJ_SCALING_SNR) {
        status = decode_and_publish_quality(&encoded, 0, outputFile, &original,
                                            has_original, &decoded);
        if (status != DIC_STATUS_OK) goto cleanup;
    }

    printf("Transfer complete: %u bytes in %.0f seconds\n", payload_size,
           difftime(time(NULL), receive_start));
    if (has_original && original.width == decoded.width &&
        original.height == decoded.height &&
        original.channels == decoded.channels) {
        double psnr = codec_metric_psnr_u8(
            original.data, decoded.data,
            dic_image_u8_sample_count(decoded.width, decoded.height,
                                      decoded.channels));
        printf("Saved reconstructed image to %s (PSNR = %.1f dB)\n", outputFile,
               psnr);
    } else {
        printf("Saved reconstructed image to %s\n", outputFile);
    }
    ok = 1;

cleanup:
    if (net != NULL) net_control_close(net);
    if (bitstream != NULL) fclose(bitstream);
    codec_basic_encoded_free(&encoded);
    dic_image_u8_free(&decoded);
    dic_image_u8_free(&original);
    return ok;
}

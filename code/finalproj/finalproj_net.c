/**
 * @file finalproj_net.c
 * @brief DICW quality-progressive network transport.
 *
 * Files and network payloads use the same layer-major DICW representation.
 * See codec/basic_file.h for the complete payload diagram.
 */

#include "finalproj/finalproj_net.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "codec/metrics.h"
#include "errors/errors.h"
#include "finalproj/finalproj_codec.h"
#include "image_u8/image_u8.h"
#include "net/net.h"
#include "net/net_platform.h"
#include "ppm/ppm.h"
#include "bits/bits.h"
#include "fs/fs.h"

/** Maximum bytes read from or written to a staging file per loop iteration. */
#define FINALPROJ_IO_CHUNK_SIZE 8192u
/** Defensive upper bound for the advertised TCP payload length. */
#define FINALPROJ_MAX_PAYLOAD_SIZE (256u * 1024u * 1024u)
/** Number of empty nonblocking receive polls allowed before timeout. */
#define FINALPROJ_RECEIVE_MAX_ATTEMPTS 10000
/** Header bytes before the variable per-channel bit-plane counts. */
#define FINALPROJ_QUALITY_FIXED_HEADER_SIZE \
    (4u + 6u * 4u + DIC_SCAN_TOKEN_COUNT + 4u)

/**
 * @brief Persistent state for parsing a growing receive-side staging file.
 *
 * @code{.unparsed}
 * staging file
 * +---------------- parsed ----------------+------ available ------+ missing
 * 0                                  position                 available
 *
 * layer/channel identify the next bit-plane block expected at position.
 * The parser advances only after a complete block or layer marker exists.
 * @endcode
 */
typedef struct receive_parser {
    /** Absolute byte offset of the next unparsed DICW field. */
    size_t position;
    /** Nonzero after metadata and per-channel counts have been allocated. */
    int header_parsed;
    /** Nonzero after the final declared layer marker has been consumed. */
    int finished;
    /** Zero-based layer currently being assembled. */
    int layer;
    /** Next channel expected within the current layer. */
    int channel;
    /** Maximum layer count declared by the DICW header. */
    int max_bitplanes;
} receive_parser;

/** @brief Overflow-safe size_t addition used for untrusted byte offsets. */
static int add_size_checked(size_t left, size_t right, size_t* result) {
    if (result == NULL || left > SIZE_MAX - right) return 0;
    *result = left + right;
    return 1;
}

/** @brief Seeks to an absolute staging-file offset representable by fseek(). */
static int file_seek_offset(FILE* file, size_t offset) {
    if (file == NULL || offset > (size_t)LONG_MAX) return 0;
    return fseek(file, (long)offset, SEEK_SET) == 0;
}

/**
 * @brief Reads an exact byte range without changing parser state.
 *
 * @code{.unparsed}
 * file: [ ... ][ requested range ][ ... ]
 *              ^ offset           ^ offset + size
 * @endcode
 */
static int file_read_at(FILE* file, size_t offset, void* output, size_t size) {
    if (output == NULL || !file_seek_offset(file, offset)) return 0;
    return size == 0u || fread(output, 1u, size, file) == size;
}

/** @brief Reads one little-endian u32 from an absolute staging-file offset. */
static int file_read_u32_at(FILE* file, size_t offset, uint32_t* value) {
    unsigned char bytes[4];
    if (value == NULL ||
        !file_read_at(file, offset, bytes, sizeof(bytes)))
        return 0;
    *value = bits_u32_from_le(bytes);
    return 1;
}

/** @brief Finds the number of layer iterations required by all channels. */
static int encoded_max_bitplanes(const codec_basic_encoded_image* encoded) {
    int maximum = 0;
    int channel;
    for (channel = 0; channel < encoded->channels; ++channel) {
        int count = encoded->channel_streams[channel].num_bitplanes;
        if (count > maximum) maximum = count;
    }
    return maximum;
}

/**
 * @brief Attempts to load one complete bit-plane from a growing file.
 *
 * @code{.unparsed}
 * position
 *   |
 *   v
 * [16-byte dominant header][dominant payload]
 * [ 8-byte run header     ][run payload]
 * [16-byte refine header  ][refine payload][0xffffffff]
 *                                                ^
 *                                                next_position
 *
 * available may end anywhere in the diagram. The function then returns
 * DIC_STATUS_OK with complete=0 and leaves @p bp unchanged.
 * @endcode
 */
static dic_status try_read_bitplane(
    FILE* file, size_t available, size_t position, size_t plane_count,
    codec_scan_bitplane* bp, size_t* next_position, int* complete) {
    uint32_t token_count, command_count, dominant_bits, dominant_bytes;
    uint32_t run_bits, run_bytes, symbols, mode, refinement_bits;
    uint32_t refinement_bytes, marker;
    size_t dominant_start, run_header, run_start, refinement_header;
    size_t refinement_start, marker_position, end;

    if (file == NULL || bp == NULL || next_position == NULL ||
        complete == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    *complete = 0;
    if (!add_size_checked(position, 16u, &dominant_start) ||
        dominant_start > available)
        return DIC_STATUS_OK;
    if (!file_read_u32_at(file, position, &token_count) ||
        !file_read_u32_at(file, position + 4u, &command_count) ||
        !file_read_u32_at(file, position + 8u, &dominant_bits) ||
        !file_read_u32_at(file, position + 12u, &dominant_bytes))
        return DIC_STATUS_FILE_READ_ERROR;
    if (token_count > plane_count || command_count > token_count ||
        (uint64_t)dominant_bits > (uint64_t)dominant_bytes * 8u ||
        !add_size_checked(dominant_start, dominant_bytes, &run_header))
        return DIC_HW4_FORMAT_ERROR;
    if (!add_size_checked(run_header, 8u, &run_start) ||
        run_start > available)
        return DIC_STATUS_OK;
    if (!file_read_u32_at(file, run_header, &run_bits) ||
        !file_read_u32_at(file, run_header + 4u, &run_bytes))
        return DIC_STATUS_FILE_READ_ERROR;
    if ((uint64_t)run_bits > (uint64_t)run_bytes * 8u ||
        !add_size_checked(run_start, run_bytes, &refinement_header))
        return DIC_HW4_FORMAT_ERROR;
    if (!add_size_checked(refinement_header, 16u, &refinement_start) ||
        refinement_start > available)
        return DIC_STATUS_OK;
    if (!file_read_u32_at(file, refinement_header, &symbols) ||
        !file_read_u32_at(file, refinement_header + 4u, &mode) ||
        !file_read_u32_at(file, refinement_header + 8u, &refinement_bits) ||
        !file_read_u32_at(file, refinement_header + 12u, &refinement_bytes))
        return DIC_STATUS_FILE_READ_ERROR;
    if (symbols > plane_count || mode > DIC_SCAN_REFINEMENT_ARITHMETIC ||
        (uint64_t)refinement_bits > (uint64_t)refinement_bytes * 8u ||
        !add_size_checked(refinement_start, refinement_bytes,
                          &marker_position) ||
        !add_size_checked(marker_position, 4u, &end))
        return DIC_HW4_FORMAT_ERROR;
    if (end > available) return DIC_STATUS_OK;
    if (!file_read_u32_at(file, marker_position, &marker))
        return DIC_STATUS_FILE_READ_ERROR;
    if (marker != DIC_BP_END_MARKER)
        return DIC_HW4_FORMAT_ERROR;

    codec_scan_bitplane_init(bp);
    bp->dominant_token_count = token_count;
    bp->dominant_command_count = command_count;
    bp->dominant_stream.bit_count = dominant_bits;
    bp->dominant_stream.byte_count = dominant_bytes;
    bp->run_length_bit_count = run_bits;
    bp->run_length_byte_count = run_bytes;
    bp->subordinate_symbol_count = symbols;
    bp->subordinate_mode = (codec_scan_refinement_mode)mode;
    bp->subordinate_bit_count = refinement_bits;
    bp->subordinate_byte_count = refinement_bytes;
    if (dominant_bytes > 0u) {
        bp->dominant_stream.bytes =
            (unsigned char*)malloc((size_t)dominant_bytes);
        if (bp->dominant_stream.bytes == NULL)
            goto memory_error;
        if (!file_read_at(file, dominant_start, bp->dominant_stream.bytes,
                          dominant_bytes))
            goto read_error;
    }
    if (run_bytes > 0u) {
        bp->run_length_bits = (unsigned char*)malloc((size_t)run_bytes);
        if (bp->run_length_bits == NULL)
            goto memory_error;
        if (!file_read_at(file, run_start, bp->run_length_bits, run_bytes))
            goto read_error;
    }
    if (refinement_bytes > 0u) {
        bp->subordinate_bits =
            (unsigned char*)malloc((size_t)refinement_bytes);
        if (bp->subordinate_bits == NULL)
            goto memory_error;
        if (!file_read_at(file, refinement_start, bp->subordinate_bits,
                          refinement_bytes))
            goto read_error;
    }
    *next_position = end;
    *complete = 1;
    return DIC_STATUS_OK;

memory_error:
    codec_scan_bitplane_free(bp);
    return DIC_STATUS_MEMORY_ERROR;
read_error:
    codec_scan_bitplane_free(bp);
    return DIC_STATUS_FILE_READ_ERROR;
}

/**
 * @brief Attempts to parse the fixed DICW header and channel counts.
 *
 * The first 28 bytes are read first so the channel count can be validated.
 * The complete header size is:
 *
 * @code{.unparsed}
 * 4 magic + 6*u32 + token_count code lengths + 1*u32 + channels*u32
 * @endcode
 */
static dic_status try_parse_quality_header(
    FILE* file, size_t available, codec_basic_encoded_image* encoded,
    receive_parser* parser) {
    unsigned char header[FINALPROJ_QUALITY_FIXED_HEADER_SIZE + 3u * 4u];
    unsigned char lengths[DIC_SCAN_TOKEN_COUNT];
    uint32_t version, width, height, channels, levels, quant_bits, maximum;
    size_t header_size;
    float quant;
    dic_status status;
    int channel;

    if (available < 28u) return DIC_STATUS_OK;
    if (!file_read_at(file, 0u, header, 28u))
        return DIC_STATUS_FILE_READ_ERROR;
    if (memcmp(header, DIC_BASIC_FILE_MAGIC, 4u) != 0)
        return DIC_HW4_FORMAT_ERROR;
    version = bits_u32_from_le(header + 4u);
    width = bits_u32_from_le(header + 8u);
    height = bits_u32_from_le(header + 12u);
    channels = bits_u32_from_le(header + 16u);
    levels = bits_u32_from_le(header + 20u);
    quant_bits = bits_u32_from_le(header + 24u);
    quant = bits_float_from_bits(quant_bits);
    if (version != DIC_BASIC_FILE_VERSION || width == 0u ||
        width > INT_MAX || height == 0u || height > INT_MAX ||
        (channels != 1u && channels != 3u) || levels == 0u ||
        levels > DIC_BASIC_MAX_LEVELS || !isfinite(quant) || quant <= 0.0f)
        return DIC_HW4_FORMAT_ERROR;
    header_size =
        FINALPROJ_QUALITY_FIXED_HEADER_SIZE + (size_t)channels * 4u;
    if (available < header_size) return DIC_STATUS_OK;
    if (!file_read_at(file, 0u, header, header_size))
        return DIC_STATUS_FILE_READ_ERROR;
    memcpy(lengths, header + 28u, DIC_SCAN_TOKEN_COUNT);
    maximum = bits_u32_from_le(header + 28u + DIC_SCAN_TOKEN_COUNT);
    if (maximum > DIC_BASIC_MAX_BITPLANES)
        return DIC_HW4_FORMAT_ERROR;

    codec_scan_set_code_lengths(lengths);
    status = codec_basic_encoded_alloc_streams(
        encoded, (int)width, (int)height, (int)channels, (int)levels, quant);
    if (status != DIC_STATUS_OK) return status;
    for (channel = 0; channel < encoded->channels; ++channel) {
        uint32_t count = bits_u32_from_le(
            header + FINALPROJ_QUALITY_FIXED_HEADER_SIZE +
            (size_t)channel * 4u);
        codec_basic_channel_stream* stream =
            encoded->channel_streams + channel;
        if (count > maximum) {
            codec_basic_encoded_free(encoded);
            return DIC_HW4_FORMAT_ERROR;
        }
        stream->num_bitplanes = (int)count;
        if (count > 0u) {
            stream->bitplanes = (codec_scan_bitplane*)calloc(
                count, sizeof(stream->bitplanes[0]));
            if (stream->bitplanes == NULL) {
                codec_basic_encoded_free(encoded);
                return DIC_STATUS_MEMORY_ERROR;
            }
        }
    }
    if ((uint32_t)encoded_max_bitplanes(encoded) != maximum) {
        codec_basic_encoded_free(encoded);
        return DIC_HW4_FORMAT_ERROR;
    }
    parser->position = header_size;
    parser->header_parsed = 1;
    parser->max_bitplanes = (int)maximum;
    parser->finished = maximum == 0u;
    printf("[receive] Header ready: %ux%u, %u channel(s), %u DWT level(s), "
           "quant=%.9g, %u quality layer(s)\n",
           width, height, channels, levels, quant, maximum);
    fflush(stdout);
    return DIC_STATUS_OK;
}

/**
 * @brief Advances through at most one complete quality layer.
 *
 * All channel bit-plane blocks are consumed first. Only a following
 * 0xfffffffe marker permits @p layer_completed to become nonzero.
 */
static dic_status parse_next_quality_layer(
    FILE* file, size_t available, codec_basic_encoded_image* encoded,
    receive_parser* parser, int* layer_completed) {
    dic_status status;

    *layer_completed = 0;
    if (!parser->header_parsed) {
        status =
            try_parse_quality_header(file, available, encoded, parser);
        if (status != DIC_STATUS_OK || !parser->header_parsed) return status;
    }
    if (parser->finished) return DIC_STATUS_OK;
    while (parser->channel < encoded->channels) {
        codec_basic_channel_stream* stream =
            encoded->channel_streams + parser->channel;
        if (parser->layer < stream->num_bitplanes) {
            int complete = 0;
            size_t next_position = parser->position;
            status = try_read_bitplane(
                file, available, parser->position,
                (size_t)encoded->width * (size_t)encoded->height,
                stream->bitplanes + parser->layer, &next_position, &complete);
            if (status != DIC_STATUS_OK || !complete) return status;
            parser->position = next_position;
        }
        parser->channel++;
    }
    if (parser->position + 4u > available) return DIC_STATUS_OK;
    {
        uint32_t marker;
        if (!file_read_u32_at(file, parser->position, &marker))
            return DIC_STATUS_FILE_READ_ERROR;
        if (marker != DIC_BASIC_LAYER_END_MARKER)
            return DIC_HW4_FORMAT_ERROR;
    }
    parser->position += 4u;
    parser->layer++;
    parser->channel = 0;
    parser->finished = parser->layer >= parser->max_bitplanes;
    *layer_completed = 1;
    return DIC_STATUS_OK;
}

/** @brief Writes through a sibling temporary path and atomic replacement. */
static dic_status write_ppm_atomic(const char* path,
                                   const dic_image_u8* image) {
    char temporary_path[1024];
    int length =
        snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path);
    dic_status status;
    if (length < 0 || (size_t)length >= sizeof(temporary_path))
        return DIC_STATUS_IO_ERROR;
    status = dic_ppm_write(temporary_path, image);
    if (status != DIC_STATUS_OK) return status;
    status = fs_replace_file(temporary_path, path);
    if (status != DIC_STATUS_OK) remove(temporary_path);
    return status;
}

/** @brief Decodes the available quality prefix and replaces the output. */
static dic_status publish_layer(const codec_basic_encoded_image* encoded,
                                int layer, const char* output_file,
                                const dic_image_u8* original,
                                int has_original) {
    dic_image_u8 decoded = {0};
    dic_status status;

    printf("[receive] Decoding quality layer %d...\n", layer);
    fflush(stdout);
    status = codec_basic_decode_image(encoded, layer, &decoded);
    if (status == DIC_STATUS_OK)
        status = write_ppm_atomic(output_file, &decoded);
    if (status == DIC_STATUS_OK && has_original &&
        original->width == decoded.width &&
        original->height == decoded.height &&
        original->channels == decoded.channels) {
        double psnr = codec_metric_psnr_u8(
            original->data, decoded.data,
            dic_image_u8_sample_count(decoded.width, decoded.height,
                                      decoded.channels));
        printf("[receive] Layer %d published to %s: %dx%d, PSNR = %.1f dB\n",
               layer, output_file, decoded.width, decoded.height, psnr);
    } else if (status == DIC_STATUS_OK) {
        printf("[receive] Layer %d published to %s: %dx%d\n", layer,
               output_file, decoded.width, decoded.height);
    }
    fflush(stdout);
    dic_image_u8_free(&decoded);
    return status;
}

/** @brief Prints transfer progress in five-percent increments. */
static void print_transfer_progress(const char* label, size_t completed,
                                    size_t total, int* last_percent) {
    int percent;
    if (total == 0u || last_percent == NULL) return;
    percent = (int)(completed * 100u / total);
    if (percent != 100 && *last_percent >= 0 &&
        percent < *last_percent + 5)
        return;
    if (percent == *last_percent) return;
    printf("%s %3d%% (%zu/%zu bytes)\n", label, percent, completed, total);
    fflush(stdout);
    *last_percent = percent;
}

/** @brief Receives an exact fixed-size framing field before payload streaming. */
static dic_status receive_all(net_control* net, uint8_t* data, size_t size) {
    size_t total = 0u;
    int attempts = 0;
    while (total < size) {
        size_t received = 0u;
        dic_status status =
            net_receive(net, data + total, size - total, &received);
        if (status != DIC_STATUS_OK) return status;
        if (received == 0u) {
            if (++attempts >= FINALPROJ_RECEIVE_MAX_ATTEMPTS)
                return DIC_NET_RECEIVE_ERROR;
            net_platform_sleep_ms(10u);
        } else {
            total += received;
            attempts = 0;
        }
    }
    return DIC_STATUS_OK;
}

/**
 * @brief Implements encode -> stage file -> buffered TCP transmission.
 *
 * Encoder memory is released before the TCP payload loop, so transmitted
 * payload bytes are read only from the staged file.
 */
int networkSend(const char* inputFile, const char* host, int port, float quant,
                uint32_t rateLimit) {
    dic_image_u8 image = {0};
    codec_basic_encoded_image encoded = {0};
    net_config config;
    net_control* net = NULL;
    FILE* stream = NULL;
    dic_status status;
    uint8_t size_header[4];
    uint8_t buffer[FINALPROJ_IO_CHUNK_SIZE];
    size_t payload_size, sent_total = 0u;
    int image_width = 0;
    int image_height = 0;
    int quality_layers = 0;
    int last_percent = -1;
    int ok = 0;

    printf("[send] Reading input image: %s\n", inputFile);
    fflush(stdout);
    status = dic_ppm_read(inputFile, &image);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "[send] failed to read input: %s\n",
                dic_status_message(status));
        goto cleanup;
    }
    image_width = image.width;
    image_height = image.height;
    printf("[send] Input ready: %dx%d, %d channel(s)\n", image.width,
           image.height, image.channels);
    printf("[send] Encoding with %d DWT level(s), quant_step=%.9g...\n",
           FINALPROJ_LEVELS, quant);
    fflush(stdout);
    status = codec_basic_encode_image(
        image.data, image.width, image.height, image.channels,
        FINALPROJ_LEVELS, quant, &encoded);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "[send] encode failed: %s\n",
                dic_status_message(status));
        goto cleanup;
    }
    quality_layers = encoded_max_bitplanes(&encoded);
    printf("[send] Encoding complete: %d quality layer(s)\n",
           quality_layers);
    printf("[send] Writing encoded DICW stream to a temporary file...\n");
    fflush(stdout);
    stream = fs_open_temp_file();
    if (stream == NULL) {
        fprintf(stderr, "[send] failed to create staging file\n");
        goto cleanup;
    }
    status = codec_basic_write_stream(stream, &encoded);
    if (status != DIC_STATUS_OK || fflush(stream) != 0 ||
        fseek(stream, 0L, SEEK_END) != 0) {
        fprintf(stderr, "[send] failed to persist encoded stream: %s\n",
                dic_status_message(status));
        goto cleanup;
    }
    {
        long end = ftell(stream);
        if (end <= 0 || (unsigned long)end > FINALPROJ_MAX_PAYLOAD_SIZE) {
            fprintf(stderr, "[send] invalid staged stream size: %ld\n", end);
            goto cleanup;
        }
        payload_size = (size_t)end;
    }
    if (fseek(stream, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "[send] failed to rewind staging file\n");
        goto cleanup;
    }
    printf("[send] Stream persisted: %zu bytes, %.4f bpp\n", payload_size,
           (double)(payload_size * 8u) /
               (double)((size_t)image_width * (size_t)image_height));
    printf("[send] Releasing encoder memory; transmission will read only "
           "from the staging file\n");
    fflush(stdout);
    codec_basic_encoded_free(&encoded);
    dic_image_u8_free(&image);

    net_config_init(&config);
    config.transport = net_TRANSPORT_TCP;
    config.host = host;
    config.peer_port = (uint16_t)port;
    config.buffer_capacity = 256u * 1024u;
    config.bytes_per_second = rateLimit;
    printf("[send] Connecting to %s:%d%s...\n", host, port,
           rateLimit > 0u ? " with rate limiting" : "");
    fflush(stdout);
    status = net_control_open(&net, &config);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "[send] connection failed: %s\n",
                dic_status_message(status));
        goto cleanup;
    }
    printf("[send] Connected. Sending 4-byte payload header (%zu bytes)...\n",
           payload_size);
    fflush(stdout);
    bits_u32_to_le((uint32_t)payload_size, size_header);
    {
        size_t sent = 0u;
        status = net_send(net, size_header, 4u, &sent);
        if (status != DIC_STATUS_OK || sent != 4u) {
            fprintf(stderr, "[send] failed to send payload header\n");
            goto cleanup;
        }
    }
    printf("[send] Streaming staged file in %u-byte chunks...\n",
           (unsigned int)FINALPROJ_IO_CHUNK_SIZE);
    fflush(stdout);
    while (sent_total < payload_size) {
        size_t wanted = payload_size - sent_total;
        size_t read_count;
        size_t sent = 0u;
        if (wanted > sizeof(buffer)) wanted = sizeof(buffer);
        read_count = fread(buffer, 1u, wanted, stream);
        if (read_count != wanted) {
            fprintf(stderr, "[send] failed to read staging file\n");
            goto cleanup;
        }
        status = net_send(net, buffer, read_count, &sent);
        if (status != DIC_STATUS_OK || sent != read_count) {
            fprintf(stderr, "[send] payload send failed: %s\n",
                    dic_status_message(status));
            goto cleanup;
        }
        sent_total += sent;
        print_transfer_progress("[send] Sent", sent_total, payload_size,
                                &last_percent);
    }
    printf("[send] Transfer complete: %zu bytes, %d quality layer(s)\n",
           payload_size, quality_layers);
    ok = 1;
cleanup:
    if (!ok)
        fprintf(stderr, "[send] network send failed\n");
    if (net != NULL) net_control_close(net);
    if (stream != NULL) fclose(stream);
    codec_basic_encoded_free(&encoded);
    dic_image_u8_free(&image);
    return ok;
}

/**
 * @brief Implements TCP -> stage file -> incremental layer publication.
 *
 * @code{.unparsed}
 * net_receive
 *    -> append and fflush staging file
 *    -> parse complete bit-plane blocks
 *    -> marker reached: decode and atomically publish
 * @endcode
 */
int networkReceive(int port, const char* outputFile,
                   const char* originalFile) {
    net_config config;
    net_control* net = NULL;
    FILE* stream = NULL;
    uint8_t size_header[4];
    uint8_t buffer[FINALPROJ_IO_CHUNK_SIZE];
    uint32_t payload_size;
    receive_parser parser;
    codec_basic_encoded_image encoded = {0};
    dic_image_u8 original = {0};
    dic_status status;
    size_t received_total = 0u;
    int attempts = 0;
    int last_percent = -1;
    int published_layers = 0;
    int has_original = 0;
    int ok = 0;
    time_t start;

    memset(&parser, 0, sizeof(parser));
    net_config_init(&config);
    config.transport = net_TRANSPORT_TCP;
    config.bind_port = (uint16_t)port;
    config.buffer_capacity = 256u * 1024u;
    status = net_control_open(&net, &config);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "[receive] failed to listen on port %d: %s\n", port,
                dic_status_message(status));
        goto cleanup;
    }
    printf("[receive] Listening on port %d...\n",
           (int)net_control_port(net));
    printf("[receive] Waiting for the 4-byte payload header...\n");
    fflush(stdout);
    status = receive_all(net, size_header, 4u);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "[receive] failed to receive payload header: %s\n",
                dic_status_message(status));
        goto cleanup;
    }
    payload_size = bits_u32_from_le(size_header);
    if (payload_size == 0u || payload_size > FINALPROJ_MAX_PAYLOAD_SIZE) {
        fprintf(stderr, "[receive] invalid payload size: %u\n", payload_size);
        goto cleanup;
    }
    stream = fs_open_temp_file();
    if (stream == NULL) {
        fprintf(stderr, "[receive] failed to create staging file\n");
        goto cleanup;
    }
    {
        const char* peer = net_control_peer_name(net);
        printf("[receive] Connection accepted%s%s\n",
               peer != NULL ? " from " : "", peer != NULL ? peer : "");
    }
    printf("[receive] Expecting %u payload bytes; incoming chunks will be "
           "persisted before parsing\n",
           payload_size);
    if (originalFile != NULL) {
        status = dic_ppm_read(originalFile, &original);
        if (status == DIC_STATUS_OK) {
            has_original = 1;
            printf("[receive] Loaded reference image for progressive PSNR: "
                   "%s\n",
                   originalFile);
        } else {
            fprintf(stderr,
                    "[receive] warning: could not load reference image %s\n",
                    originalFile);
        }
    }
    fflush(stdout);
    start = time(NULL);
    while (received_total < (size_t)payload_size) {
        size_t capacity = (size_t)payload_size - received_total;
        size_t received = 0u;
        if (capacity > sizeof(buffer)) capacity = sizeof(buffer);
        status = net_receive(net, buffer, capacity, &received);
        if (status != DIC_STATUS_OK) {
            fprintf(stderr, "[receive] payload receive failed: %s\n",
                    dic_status_message(status));
            goto cleanup;
        }
        if (received == 0u) {
            if (++attempts >= FINALPROJ_RECEIVE_MAX_ATTEMPTS) {
                fprintf(stderr, "[receive] receive timed out\n");
                goto cleanup;
            }
            net_platform_sleep_ms(10u);
            continue;
        }
        attempts = 0;
        if (fseek(stream, 0L, SEEK_END) != 0 ||
            fwrite(buffer, 1u, received, stream) != received ||
            fflush(stream) != 0) {
            fprintf(stderr, "[receive] failed to append staging file\n");
            goto cleanup;
        }
        received_total += received;
        print_transfer_progress("[receive] Received", received_total,
                                payload_size, &last_percent);

        while (1) {
            int layer_completed = 0;
            status = parse_next_quality_layer(
                stream, received_total, &encoded, &parser, &layer_completed);
            if (status != DIC_STATUS_OK) {
                fprintf(stderr,
                        "[receive] invalid progressive stream near byte %zu: "
                        "%s\n",
                        parser.position, dic_status_message(status));
                goto cleanup;
            }
            if (!layer_completed) break;
            published_layers = parser.layer;
            printf("[receive] Layer marker %d/%d complete at byte %zu; "
                   "decoding immediately\n",
                   parser.layer, parser.max_bitplanes, parser.position);
            fflush(stdout);
            status = publish_layer(&encoded, parser.layer, outputFile,
                                   &original, has_original);
            if (status != DIC_STATUS_OK) {
                fprintf(stderr,
                        "[receive] layer %d decode/write failed: %s\n",
                        parser.layer, dic_status_message(status));
                goto cleanup;
            }
        }
    }
    while (!parser.finished) {
        int layer_completed = 0;
        status = parse_next_quality_layer(
            stream, received_total, &encoded, &parser, &layer_completed);
        if (status != DIC_STATUS_OK || !layer_completed) {
            fprintf(stderr,
                    "[receive] payload ended before a complete layer marker\n");
            goto cleanup;
        }
        published_layers = parser.layer;
        printf("[receive] Layer marker %d/%d complete at byte %zu; "
               "decoding immediately\n",
               parser.layer, parser.max_bitplanes, parser.position);
        status = publish_layer(&encoded, parser.layer, outputFile, &original,
                               has_original);
        if (status != DIC_STATUS_OK) goto cleanup;
    }
    if (!parser.header_parsed || parser.position != received_total) {
        fprintf(stderr,
                "[receive] stream ended with %zu unparsed trailing byte(s)\n",
                received_total - parser.position);
        goto cleanup;
    }
    if (published_layers == 0) {
        printf("[receive] Stream has no nonzero bit-plane layers; publishing "
               "zero reconstruction\n");
        status = publish_layer(&encoded, 0, outputFile, &original,
                               has_original);
        if (status != DIC_STATUS_OK) goto cleanup;
    }
    printf("[receive] Transfer complete: %u bytes, %d layer(s) published in "
           "%.0f seconds\n",
           payload_size, published_layers,
           difftime(time(NULL), start));
    ok = 1;
cleanup:
    if (!ok)
        fprintf(stderr, "[receive] network receive failed\n");
    if (net != NULL) net_control_close(net);
    if (stream != NULL) fclose(stream);
    codec_basic_encoded_free(&encoded);
    dic_image_u8_free(&original);
    return ok;
}

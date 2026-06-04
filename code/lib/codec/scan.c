/**
 * @file scan.c
 * @brief Implements EZW-style per-bitplane zerotree scan encoding and decoding.
 */

#include "codec/scan.h"

#include <stdlib.h>
#include <string.h>

#include "codec/subband.h"
#include "wavelet/dic_dwt53.h"

/* -------------------------------------------------------------------------- */
/*  Internal helpers                                                          */
/* -------------------------------------------------------------------------- */

static size_t codec_scan_index(int width, int x, int y)
{
    return ((size_t)y * (size_t)width) + (size_t)x;
}

static int codec_scan_find_max_bitplane(const int32_t *plane, size_t count)
{
    uint32_t max_mag = 0u;
    size_t i;

    for (i = 0; i < count; ++i)
    {
        uint32_t mag = plane[i] < 0
            ? (uint32_t)(-(plane[i] + 1)) + 1u
            : (uint32_t)plane[i];
        if (mag > max_mag)
            max_mag = mag;
    }

    if (max_mag == 0u)
        return -1;

    {
        int bp = -1;
        while (max_mag > 0u) { ++bp; max_mag >>= 1; }
        return bp;
    }
}

static int codec_scan_descendants_insignificant(
    const int32_t *plane, int width, int height, int level,
    codec_subband_orientation orientation,
    int local_x, int local_y, int32_t threshold)
{
    dic_rect_i32 child_rect;
    int dx, dy;

    if (level <= 1)
        return 1;

    if (codec_subband_rect(width, height, level, level - 1, orientation, &child_rect)
        != DIC_STATUS_OK)
        return 0;

    for (dy = 0; dy < 2; ++dy)
    {
        for (dx = 0; dx < 2; ++dx)
        {
            int child_x = (local_x * 2) + dx;
            int child_y = (local_y * 2) + dy;
            int image_x, image_y;
            int32_t child_val;

            if (child_x >= child_rect.width || child_y >= child_rect.height)
                continue;

            image_x = child_rect.x + child_x;
            image_y = child_rect.y + child_y;
            child_val = plane[codec_scan_index(width, image_x, image_y)];

            if (child_val >= threshold || child_val <= -threshold)
                return 0;

            if (!codec_scan_descendants_insignificant(
                    plane, width, height, level - 1,
                    orientation, child_x, child_y, threshold))
                return 0;
        }
    }

    return 1;
}

static void codec_scan_mark_descendants_visited(
    unsigned char *visited, int width, int height, int level,
    codec_subband_orientation orientation,
    int local_x, int local_y)
{
    dic_rect_i32 child_rect;
    int dx, dy;

    if (level <= 1)
        return;

    if (codec_subband_rect(width, height, level, level - 1, orientation, &child_rect)
        != DIC_STATUS_OK)
        return;

    for (dy = 0; dy < 2; ++dy)
    {
        for (dx = 0; dx < 2; ++dx)
        {
            int child_x = (local_x * 2) + dx;
            int child_y = (local_y * 2) + dy;
            int image_x, image_y;

            if (child_x >= child_rect.width || child_y >= child_rect.height)
                continue;

            image_x = child_rect.x + child_x;
            image_y = child_rect.y + child_y;
            visited[codec_scan_index(width, image_x, image_y)] = 1u;
            codec_scan_mark_descendants_visited(
                visited, width, height, level - 1,
                orientation, child_x, child_y);
        }
    }
}

/* -------------------------------------------------------------------------- */
/*  Bit-level I/O                                                             */
/* -------------------------------------------------------------------------- */

typedef struct codec_scan_bit_writer
{
    unsigned char *bytes;
    size_t byte_capacity;
    size_t bit_count;
    unsigned char current_byte;
    int bit_pos;
} codec_scan_bit_writer;

static void codec_scan_bit_writer_init(codec_scan_bit_writer *w)
{
    if (w == NULL) return;
    w->bytes = NULL;
    w->byte_capacity = 0u;
    w->bit_count = 0u;
    w->current_byte = 0u;
    w->bit_pos = 0;
}

static dic_status codec_scan_bit_writer_ensure(size_t total_bits, codec_scan_bit_writer *w)
{
    size_t needed = (total_bits + 7u) / 8u;
    if (w == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    if (needed <= w->byte_capacity) return DIC_STATUS_OK;
    {
        size_t cap = w->byte_capacity == 0u ? 64u : w->byte_capacity;
        while (cap < needed) {
            if (cap > SIZE_MAX / 2u) { cap = needed; break; }
            cap *= 2u;
        }
        {
            unsigned char *tmp = (unsigned char *)realloc(w->bytes, cap);
            if (tmp == NULL) return DIC_STATUS_MEMORY_ERROR;
            w->bytes = tmp;
            w->byte_capacity = cap;
        }
    }
    return DIC_STATUS_OK;
}

static void codec_scan_bit_write(int bit, codec_scan_bit_writer *w)
{
    if (w == NULL) return;
    w->current_byte |= (unsigned char)((bit & 1) << w->bit_pos);
    ++w->bit_pos;
    ++w->bit_count;
    if (w->bit_pos == 8) {
        w->bytes[w->bit_count / 8u - 1u] = w->current_byte;
        w->current_byte = 0u;
        w->bit_pos = 0;
    }
}

static void codec_scan_bit_writer_flush(codec_scan_bit_writer *w)
{
    if (w == NULL || w->bit_pos == 0) return;
    w->bytes[w->bit_count / 8u] = w->current_byte;
    w->current_byte = 0u;
    w->bit_pos = 0;
}

typedef struct codec_scan_bit_reader
{
    const unsigned char *bytes;
    size_t byte_count;
    size_t bit_count;
    size_t bits_read;
} codec_scan_bit_reader;

static void codec_scan_bit_reader_init(
    codec_scan_bit_reader *r, const unsigned char *bytes, size_t bit_count)
{
    if (r == NULL) return;
    r->bytes = bytes;
    r->byte_count = (bit_count + 7u) / 8u;
    r->bit_count = bit_count;
    r->bits_read = 0u;
}

static int codec_scan_bit_read(codec_scan_bit_reader *r)
{
    size_t byte_idx, bit_idx;
    if (r == NULL || r->bits_read >= r->bit_count) return 0;
    byte_idx = r->bits_read / 8u;
    bit_idx  = r->bits_read % 8u;
    ++r->bits_read;
    return (int)((r->bytes[byte_idx] >> bit_idx) & 1u);
}

/* -------------------------------------------------------------------------- */
/*  Token buffer                                                              */
/* -------------------------------------------------------------------------- */

typedef struct codec_scan_token_buffer
{
    unsigned char *tokens;
    size_t count;
    size_t capacity;
} codec_scan_token_buffer;

static void codec_scan_token_buffer_init(codec_scan_token_buffer *buf)
{
    if (buf == NULL) return;
    buf->tokens = NULL;
    buf->count = 0u;
    buf->capacity = 0u;
}

static void codec_scan_token_buffer_free(codec_scan_token_buffer *buf)
{
    if (buf == NULL) return;
    free(buf->tokens);
    codec_scan_token_buffer_init(buf);
}

static dic_status codec_scan_token_buffer_append(
    codec_scan_token_buffer *buf, unsigned char token)
{
    if (buf == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    if (buf->count >= buf->capacity) {
        size_t cap = buf->capacity == 0u ? 256u : buf->capacity;
        while (cap <= buf->count) {
            if (cap > SIZE_MAX / 2u) { cap = buf->count + 1u; break; }
            cap *= 2u;
        }
        {
            unsigned char *tmp = (unsigned char *)realloc(buf->tokens, cap);
            if (tmp == NULL) return DIC_STATUS_MEMORY_ERROR;
            buf->tokens = tmp;
            buf->capacity = cap;
        }
    }
    buf->tokens[buf->count] = token;
    ++buf->count;
    return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Significance-order tracking                                               */
/* -------------------------------------------------------------------------- */

typedef struct codec_scan_sig_order
{
    size_t *indices;
    size_t count;
    size_t capacity;
} codec_scan_sig_order;

static void codec_scan_sig_order_init(codec_scan_sig_order *order)
{
    if (order == NULL) return;
    order->indices = NULL;
    order->count = 0u;
    order->capacity = 0u;
}

static void codec_scan_sig_order_free(codec_scan_sig_order *order)
{
    if (order == NULL) return;
    free(order->indices);
    codec_scan_sig_order_init(order);
}

static dic_status codec_scan_sig_order_append(codec_scan_sig_order *order, size_t index)
{
    if (order == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    if (order->count >= order->capacity) {
        size_t cap = order->capacity == 0u ? 256u : order->capacity;
        while (cap <= order->count) {
            if (cap > SIZE_MAX / 2u) { cap = order->count + 1u; break; }
            cap *= 2u;
        }
        {
            size_t *tmp = (size_t *)realloc(order->indices, cap * sizeof(order->indices[0]));
            if (tmp == NULL) return DIC_STATUS_MEMORY_ERROR;
            order->indices = tmp;
            order->capacity = cap;
        }
    }
    order->indices[order->count] = index;
    ++order->count;
    return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Huffman helpers                                                           */
/* -------------------------------------------------------------------------- */

static unsigned int codec_scan_map_token(const void *element)
{
    return (unsigned int)(*(const unsigned char *)element);
}

static void codec_scan_write_token_fn(void *element, unsigned int symbol)
{
    *(unsigned char *)element = (unsigned char)symbol;
}

static dic_status codec_scan_huffman_encode(
    const size_t *freq, const unsigned char *tokens, size_t token_count,
    dic_hw2_huffman_bitstream *bitstream)
{
    dic_hw2_huffman_tree tree;
    dic_status status;
    dic_hw2_huffman_tree_init(&tree);
    dic_hw2_huffman_bitstream_init(bitstream);
    status = dic_hw2_huffman_build_from_counts(freq, DIC_SCAN_TOKEN_COUNT, &tree);
    if (status != DIC_STATUS_OK) { dic_hw2_huffman_tree_free(&tree); return status; }
    status = dic_hw2_huffman_encode_mapped(
        &tree, tokens, token_count, sizeof(tokens[0]), codec_scan_map_token, bitstream);
    dic_hw2_huffman_tree_free(&tree);
    return status;
}

static dic_status codec_scan_huffman_decode(
    const size_t *freq, const dic_hw2_huffman_bitstream *bitstream,
    unsigned char *tokens, size_t token_count)
{
    dic_hw2_huffman_tree tree;
    dic_status status;
    dic_hw2_huffman_tree_init(&tree);
    status = dic_hw2_huffman_build_from_counts(freq, DIC_SCAN_TOKEN_COUNT, &tree);
    if (status != DIC_STATUS_OK) { dic_hw2_huffman_tree_free(&tree); return status; }
    status = dic_hw2_huffman_decode_mapped(
        &tree, bitstream, token_count, tokens, sizeof(tokens[0]), codec_scan_write_token_fn);
    dic_hw2_huffman_tree_free(&tree);
    return status;
}

/* -------------------------------------------------------------------------- */
/*  Significance pass encoder (one bitplane)                                  */
/* -------------------------------------------------------------------------- */

static dic_status codec_scan_encode_significance_pass(
    int32_t *plane, int width, int height, int levels,
    int32_t threshold, unsigned char *significant, unsigned char *visited,
    codec_scan_token_buffer *tokens, codec_scan_sig_order *sig_order)
{
    dic_rect_i32 ll_rect;
    dic_status status;
    int level;

    status = codec_subband_lowest_ll_rect(width, height, levels, &ll_rect);
    if (status != DIC_STATUS_OK) return status;

    /* LL subband — no zerotree */
    {
        int y;
        for (y = 0; y < ll_rect.height; ++y) {
            int x;
            for (x = 0; x < ll_rect.width; ++x) {
                size_t idx = codec_scan_index(width, ll_rect.x + x, ll_rect.y + y);
                int32_t val;
                unsigned char token;

                if (significant[idx] || visited[idx]) continue;

                val = plane[idx];
                if (val >= threshold || val <= -threshold) {
                    if (val > 0) {
                        token = (unsigned char)DIC_SCAN_TOKEN_POS;
                        plane[idx] = val - threshold;
                    } else {
                        token = (unsigned char)DIC_SCAN_TOKEN_NEG;
                        plane[idx] = val + threshold;
                    }
                    significant[idx] = 1u;
                    status = codec_scan_sig_order_append(sig_order, idx);
                } else {
                    token = (unsigned char)DIC_SCAN_TOKEN_IZ;
                }
                if (status == DIC_STATUS_OK)
                    status = codec_scan_token_buffer_append(tokens, token);
                if (status != DIC_STATUS_OK) return status;
            }
        }
    }

    /* High-pass bands from coarsest to finest */
    for (level = levels; level >= 1; --level) {
        int band;
        codec_subband_orientation orientations[3] = {
            DIC_SUBBAND_HL, DIC_SUBBAND_LH, DIC_SUBBAND_HH
        };
        for (band = 0; band < 3; ++band) {
            codec_subband_orientation orient = orientations[band];
            dic_rect_i32 rect;
            int y;

            status = codec_subband_rect(width, height, levels, level, orient, &rect);
            if (status != DIC_STATUS_OK) return status;

            for (y = 0; y < rect.height; ++y) {
                int x;
                for (x = 0; x < rect.width; ++x) {
                    int image_x = rect.x + x;
                    int image_y = rect.y + y;
                    size_t idx = codec_scan_index(width, image_x, image_y);
                    int32_t val;
                    unsigned char token;

                    if (significant[idx] || visited[idx]) continue;

                    val = plane[idx];
                    if (val >= threshold || val <= -threshold) {
                        if (val > 0) {
                            token = (unsigned char)DIC_SCAN_TOKEN_POS;
                            plane[idx] = val - threshold;
                        } else {
                            token = (unsigned char)DIC_SCAN_TOKEN_NEG;
                            plane[idx] = val + threshold;
                        }
                        significant[idx] = 1u;
                        status = codec_scan_sig_order_append(sig_order, idx);
                    } else {
                        int is_ztr = 0;
                        if (level > 1)
                            is_ztr = codec_scan_descendants_insignificant(
                                plane, width, height, level, orient, x, y, threshold);
                        if (is_ztr) {
                            token = (unsigned char)DIC_SCAN_TOKEN_ZTR;
                            codec_scan_mark_descendants_visited(
                                visited, width, height, level, orient, x, y);
                        } else {
                            token = (unsigned char)DIC_SCAN_TOKEN_IZ;
                        }
                    }
                    if (status == DIC_STATUS_OK)
                        status = codec_scan_token_buffer_append(tokens, token);
                    if (status != DIC_STATUS_OK) return status;
                }
            }
        }
    }
    return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Refinement pass encoder (one bitplane)                                    */
/* -------------------------------------------------------------------------- */

static dic_status codec_scan_encode_refinement_pass(
    const int32_t *plane, int bp,
    const codec_scan_sig_order *sig_order, size_t refinement_count,
    codec_scan_bit_writer *bit_writer)
{
    size_t i;
    for (i = 0; i < refinement_count; ++i) {
        size_t idx = sig_order->indices[i];
        uint32_t mag = plane[idx] < 0
            ? (uint32_t)(-(plane[idx] + 1)) + 1u : (uint32_t)plane[idx];
        int bit = (int)((mag >> bp) & 1u);
        codec_scan_bit_write(bit, bit_writer);
    }
    return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Refinement pass decoder (one bitplane)                                    */
/* -------------------------------------------------------------------------- */

static dic_status codec_scan_decode_refinement_pass(
    codec_scan_bit_reader *reader, int bp, int32_t *plane,
    const size_t *sig_order, size_t refinement_count)
{
    size_t i;
    for (i = 0; i < refinement_count; ++i) {
        size_t idx = sig_order[i];
        int32_t val = plane[idx];
        int bit = codec_scan_bit_read(reader);
        uint32_t mag = val < 0
            ? (uint32_t)(-(val + 1)) + 1u : (uint32_t)val;
        if (bit) mag |= ((uint32_t)1u << (unsigned)bp);
        plane[idx] = (val < 0) ? -(int32_t)mag : (int32_t)mag;
    }
    return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

void codec_scan_bitplane_init(codec_scan_bitplane *bp)
{
    if (bp == NULL) return;
    bp->dominant_token_count = 0u;
    memset(bp->token_freq, 0, sizeof(bp->token_freq));
    dic_hw2_huffman_bitstream_init(&bp->dominant_stream);
    bp->subordinate_bits = NULL;
    bp->subordinate_bit_count = 0u;
    bp->subordinate_byte_count = 0u;
}

void codec_scan_bitplane_free(codec_scan_bitplane *bp)
{
    if (bp == NULL) return;
    dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
    free(bp->subordinate_bits);
    codec_scan_bitplane_init(bp);
}

unsigned char codec_scan_amplitude_size(int32_t amplitude)
{
    uint32_t magnitude;
    unsigned char bits = 0u;
    if (amplitude == 0) return 0u;
    magnitude = amplitude < 0
        ? (uint32_t)(-(amplitude + 1)) + 1u : (uint32_t)amplitude;
    while (magnitude != 0u) { ++bits; magnitude >>= 1; }
    return bits;
}

dic_status codec_scan_encode_plane(
    const int32_t *plane, int width, int height, int levels,
    codec_scan_bitplane **bitplanes_out, int *bitplane_count_out)
{
    size_t plane_count;
    int max_bp, total_bp;
    codec_scan_bitplane *bps = NULL;
    unsigned char *significant = NULL;
    codec_scan_sig_order sig_order;
    dic_status status = DIC_STATUS_OK;
    int32_t *work_plane = NULL;
    int bp;

    if (plane == NULL || bitplanes_out == NULL || bitplane_count_out == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    *bitplanes_out = NULL;
    *bitplane_count_out = 0;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK) return status;

    plane_count = (size_t)width * (size_t)height;

    max_bp = codec_scan_find_max_bitplane(plane, plane_count);
    if (max_bp < 0) {
        /* All zero — return 0 bitplanes */
        return DIC_STATUS_OK;
    }

    total_bp = max_bp + 1;
    bps = (codec_scan_bitplane *)calloc((size_t)total_bp, sizeof(bps[0]));
    if (bps == NULL) return DIC_STATUS_MEMORY_ERROR;

    /* Work on a copy (EZW modifies magnitudes in-place) */
    work_plane = (int32_t *)malloc(plane_count * sizeof(work_plane[0]));
    if (work_plane == NULL) { free(bps); return DIC_STATUS_MEMORY_ERROR; }
    memcpy(work_plane, plane, plane_count * sizeof(plane[0]));

    significant = (unsigned char *)calloc(plane_count, 1u);
    if (significant == NULL) { free(work_plane); free(bps); return DIC_STATUS_MEMORY_ERROR; }

    codec_scan_sig_order_init(&sig_order);

    for (bp = max_bp; bp >= 0; --bp) {
        int32_t threshold = (int32_t)(1u << (unsigned)bp);
        codec_scan_token_buffer token_buf;
        codec_scan_bit_writer bit_writer;
        unsigned char *visited = NULL;
        size_t sig_before = sig_order.count;
        codec_scan_bitplane *cur = bps + (max_bp - bp); /* MSB at index 0 */
        size_t k;

        codec_scan_token_buffer_init(&token_buf);
        codec_scan_bit_writer_init(&bit_writer);

        visited = (unsigned char *)calloc(plane_count, 1u);
        if (visited == NULL) {
            codec_scan_token_buffer_free(&token_buf);
            status = DIC_STATUS_MEMORY_ERROR;
            break;
        }

        /* Significance pass */
        status = codec_scan_encode_significance_pass(
            work_plane, width, height, levels, threshold,
            significant, visited, &token_buf, &sig_order);
        free(visited);
        if (status != DIC_STATUS_OK) {
            codec_scan_token_buffer_free(&token_buf);
            break;
        }

        /* Refinement pass (only coefficients significant BEFORE this bp) */
        status = codec_scan_bit_writer_ensure(sig_before, &bit_writer);
        if (status == DIC_STATUS_OK && sig_before > 0u) {
            size_t saved = sig_order.count;
            sig_order.count = sig_before;
            status = codec_scan_encode_refinement_pass(
                work_plane, bp, &sig_order, sig_before, &bit_writer);
            sig_order.count = saved;
        }
        if (status == DIC_STATUS_OK)
            codec_scan_bit_writer_flush(&bit_writer);

        if (status != DIC_STATUS_OK) {
            free(bit_writer.bytes);
            codec_scan_token_buffer_free(&token_buf);
            break;
        }

        /* Token frequencies */
        memset(cur->token_freq, 0, sizeof(cur->token_freq));
        for (k = 0; k < token_buf.count; ++k) {
            unsigned char tok = token_buf.tokens[k];
            if (tok < DIC_SCAN_TOKEN_COUNT) ++cur->token_freq[tok];
        }
        cur->dominant_token_count = token_buf.count;

        /* Huffman-encode tokens */
        status = codec_scan_huffman_encode(
            cur->token_freq, token_buf.tokens, token_buf.count, &cur->dominant_stream);

        if (status == DIC_STATUS_OK) {
            /* Transfer subordinate bits */
            cur->subordinate_bits = bit_writer.bytes;
            cur->subordinate_bit_count = bit_writer.bit_count;
            cur->subordinate_byte_count = (bit_writer.bit_count + 7u) / 8u;
        } else {
            free(bit_writer.bytes);
        }

        codec_scan_token_buffer_free(&token_buf);
        if (status != DIC_STATUS_OK) break;
    }

    codec_scan_sig_order_free(&sig_order);
    free(significant);
    free(work_plane);

    if (status != DIC_STATUS_OK) {
        int i;
        for (i = 0; i < total_bp; ++i) codec_scan_bitplane_free(bps + i);
        free(bps);
        return status;
    }

    *bitplanes_out = bps;
    *bitplane_count_out = total_bp;
    return DIC_STATUS_OK;
}

dic_status codec_scan_decode_plane(
    const codec_scan_bitplane *bitplanes, int total_bitplane_count, int num_bitplanes,
    int width, int height, int levels, int32_t *plane)
{
    size_t plane_count;
    unsigned char *significant = NULL;
    codec_scan_sig_order sig_order;
    dic_status status;
    int bp_idx;

    if (bitplanes == NULL || plane == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (num_bitplanes <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK) return status;

    plane_count = (size_t)width * (size_t)height;
    memset(plane, 0, plane_count * sizeof(plane[0]));

    significant = (unsigned char *)calloc(plane_count, 1u);
    if (significant == NULL) return DIC_STATUS_MEMORY_ERROR;

    codec_scan_sig_order_init(&sig_order);

    for (bp_idx = 0; bp_idx < num_bitplanes; ++bp_idx) {
        const codec_scan_bitplane *cur = bitplanes + bp_idx;
        /* The first bitplane is MSB (max_bp). We don't need the absolute bp
         * value for decoding — the reconstruction is additive.  Each bitplane
         * contributes its threshold to newly-significant coefficients.
         * We compute threshold from bp_idx and global max_bp: since we no longer
         * store max_bp explicitly in the bitplane struct, we compute it from
         * the total count. */
        int bp = num_bitplanes - 1 - bp_idx; /* MSB=num_bitplanes-1, LSB=0 */
        int32_t threshold = (int32_t)(1u << (unsigned)bp);
        unsigned char *tokens = NULL;
        unsigned char *visited = NULL;
        size_t token_offset = 0u;
        size_t sig_before = sig_order.count;

        /* Decode Huffman tokens */
        tokens = (unsigned char *)malloc(cur->dominant_token_count);
        if (tokens == NULL && cur->dominant_token_count > 0u) {
            status = DIC_STATUS_MEMORY_ERROR;
            goto cleanup;
        }
        status = codec_scan_huffman_decode(
            cur->token_freq, &cur->dominant_stream, tokens, cur->dominant_token_count);
        if (status != DIC_STATUS_OK) { free(tokens); goto cleanup; }

        visited = (unsigned char *)calloc(plane_count, 1u);
        if (visited == NULL) { free(tokens); status = DIC_STATUS_MEMORY_ERROR; goto cleanup; }

        /* Significance pass decode */
        {
            dic_rect_i32 ll_rect;
            int level;

            status = codec_subband_lowest_ll_rect(width, height, levels, &ll_rect);
            if (status != DIC_STATUS_OK) { free(tokens); free(visited); goto cleanup; }

            /* LL subband */
            {
                int y;
                for (y = 0; y < ll_rect.height && status == DIC_STATUS_OK; ++y) {
                    int x;
                    for (x = 0; x < ll_rect.width; ++x) {
                        size_t idx = codec_scan_index(width, ll_rect.x + x, ll_rect.y + y);
                        if (significant[idx] || visited[idx]) continue;
                        if (token_offset >= cur->dominant_token_count) {
                            status = DIC_HW4_FORMAT_ERROR; break;
                        }
                        {
                            unsigned char token = tokens[token_offset++];
                            if (token == (unsigned char)DIC_SCAN_TOKEN_POS) {
                                plane[idx] = threshold;
                                significant[idx] = 1u;
                                status = codec_scan_sig_order_append(&sig_order, idx);
                            } else if (token == (unsigned char)DIC_SCAN_TOKEN_NEG) {
                                plane[idx] = -threshold;
                                significant[idx] = 1u;
                                status = codec_scan_sig_order_append(&sig_order, idx);
                            } else if (token != (unsigned char)DIC_SCAN_TOKEN_IZ) {
                                status = DIC_HW4_FORMAT_ERROR;
                            }
                        }
                    }
                }
            }

            /* High-pass bands */
            if (status == DIC_STATUS_OK) {
                for (level = levels; level >= 1; --level) {
                    int band;
                    codec_subband_orientation orientations[3] = {
                        DIC_SUBBAND_HL, DIC_SUBBAND_LH, DIC_SUBBAND_HH
                    };
                    for (band = 0; band < 3 && status == DIC_STATUS_OK; ++band) {
                        codec_subband_orientation orient = orientations[band];
                        dic_rect_i32 rect;
                        int y;
                        status = codec_subband_rect(width, height, levels, level, orient, &rect);
                        if (status != DIC_STATUS_OK) break;
                        for (y = 0; y < rect.height && status == DIC_STATUS_OK; ++y) {
                            int x;
                            for (x = 0; x < rect.width; ++x) {
                                int image_x = rect.x + x;
                                int image_y = rect.y + y;
                                size_t idx = codec_scan_index(width, image_x, image_y);
                                if (significant[idx] || visited[idx]) continue;
                                if (token_offset >= cur->dominant_token_count) {
                                    status = DIC_HW4_FORMAT_ERROR; break;
                                }
                                {
                                    unsigned char token = tokens[token_offset++];
                                    if (token == (unsigned char)DIC_SCAN_TOKEN_POS) {
                                        plane[idx] = threshold;
                                        significant[idx] = 1u;
                                        status = codec_scan_sig_order_append(&sig_order, idx);
                                    } else if (token == (unsigned char)DIC_SCAN_TOKEN_NEG) {
                                        plane[idx] = -threshold;
                                        significant[idx] = 1u;
                                        status = codec_scan_sig_order_append(&sig_order, idx);
                                    } else if (token == (unsigned char)DIC_SCAN_TOKEN_ZTR) {
                                        visited[idx] = 1u;
                                        codec_scan_mark_descendants_visited(
                                            visited, width, height, level, orient, x, y);
                                    } else if (token != (unsigned char)DIC_SCAN_TOKEN_IZ) {
                                        status = DIC_HW4_FORMAT_ERROR;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        free(tokens);
        free(visited);
        if (status != DIC_STATUS_OK) goto cleanup;

        /* Refinement pass — only coefficients significant BEFORE this bp */
        if (cur->subordinate_bit_count > 0u) {
            codec_scan_bit_reader reader;
            codec_scan_bit_reader_init(&reader, cur->subordinate_bits, cur->subordinate_bit_count);
            status = codec_scan_decode_refinement_pass(
                &reader, bp, plane, sig_order.indices, sig_before);
            if (status != DIC_STATUS_OK) goto cleanup;
        }
    }

    /* Midpoint reconstruction when decoding fewer than all bitplanes */
    if (num_bitplanes < total_bitplane_count) {
        /* last_decoded_bp = max_bp - (num_bitplanes - 1)
         *                  = (total - 1) - (num - 1) = total - num
         * Midpoint = 2^(last_decoded_bp - 1) = 2^(total - num - 1) */
        int last_decoded_bp = total_bitplane_count - num_bitplanes;
        if (last_decoded_bp > 0) {
            int32_t midpoint = (int32_t)(1u << (unsigned)(last_decoded_bp - 1));
            size_t idx;
            for (idx = 0; idx < plane_count; ++idx) {
                if (plane[idx] > 0)
                    plane[idx] += midpoint;
                else if (plane[idx] < 0)
                    plane[idx] -= midpoint;
            }
        }
    }

    status = DIC_STATUS_OK;

cleanup:
    codec_scan_sig_order_free(&sig_order);
    free(significant);
    return status;
}

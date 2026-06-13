/**
 * @file scan.c
 * @brief Implements EZW-style per-bitplane zerotree scan encoding and decoding.
 *
 * Bits produced by the local writer are packed least-significant-bit first:
 *
 * @code{.unparsed}
 * logical bit order: b0 b1 b2 b3 b4 b5 b6 b7 b8 ...
 * byte[0] bits:      0  1  2  3  4  5  6  7
 * byte[1] bits:      8  9 ...
 * @endcode
 */

#include "codec/scan.h"

#include <stdlib.h>
#include <string.h>

#include "bits/bits.h"
#include "codec/subband.h"
#include "vec/vec.h"
#include "wavelet/dic_dwt53.h"

// fixed huffman table
const double codec_scan_fixed_probs[DIC_SCAN_TOKEN_COUNT] = {
    0.425, /* IZ */
    0.153, /* ZTR */
    0.229, /* POS */
    0.193  /* NEG */
};

inline static size_t codec_scan_index(int width, int x, int y) {
    return ((size_t)y * (size_t)width) + (size_t)x;
}

typedef enum codec_scan_phase {
    /** Visit the lowest LL subband in row-major order. */
    CODEC_SCAN_PHASE_LL,
    /** Visit high-pass bands from coarse to fine in HL, LH, HH order. */
    CODEC_SCAN_PHASE_HIGH_PASS,
    /** No scan positions remain. */
    CODEC_SCAN_PHASE_DONE
} codec_scan_phase;

/**
 * @brief Stateful iterator over the packed wavelet coefficient scan order.
 *
 * The cursor starts in the lowest LL rectangle, then advances through each
 * high-pass rectangle. Within a rectangle, x and y always identify the next
 * row-major coefficient to return.
 */
typedef struct codec_scan_cursor {
    int width;
    int height;
    int levels;
    codec_scan_phase phase;
    int level;
    int band;
    int x;
    int y;
    dic_rect_i32 rect;
} codec_scan_cursor;

/**
 * @brief Metadata for one coefficient produced by the scan cursor.
 *
 * local_x and local_y are relative to the current subband, which allows the
 * zerotree code to find descendants without reconstructing scan state.
 */
typedef struct codec_scan_position {
    size_t index;
    int local_x;
    int local_y;
    int level;
    codec_subband_orientation orientation;
    int is_ll;
} codec_scan_position;

static const codec_subband_orientation codec_scan_orientations[3] = {
    DIC_SUBBAND_HL, DIC_SUBBAND_LH, DIC_SUBBAND_HH};

/**
 * @brief Selects the next non-empty high-pass rectangle.
 *
 * Exhausting HH advances to the next finer level. Exhausting level one moves
 * the cursor to DONE.
 */
static dic_status codec_scan_cursor_load_high_pass(codec_scan_cursor* cursor) {
    while (cursor->level >= 1) {
        dic_status status;

        if (cursor->band >= 3) {
            --cursor->level;
            cursor->band = 0;
            continue;
        }

        status = codec_subband_rect(
            cursor->width, cursor->height, cursor->levels, cursor->level,
            codec_scan_orientations[cursor->band], &cursor->rect);
        if (status != DIC_STATUS_OK) return status;

        cursor->x = 0;
        cursor->y = 0;
        if (cursor->rect.width > 0 && cursor->rect.height > 0)
            return DIC_STATUS_OK;
        ++cursor->band;
    }

    cursor->phase = CODEC_SCAN_PHASE_DONE;
    return DIC_STATUS_OK;
}

/** @brief Initializes a cursor at the first coefficient in scan order. */
static dic_status codec_scan_cursor_init(codec_scan_cursor* cursor, int width,
                                         int height, int levels) {
    dic_status status;

    cursor->width = width;
    cursor->height = height;
    cursor->levels = levels;
    cursor->phase = CODEC_SCAN_PHASE_LL;
    cursor->level = levels;
    cursor->band = 0;
    cursor->x = 0;
    cursor->y = 0;

    status = codec_subband_lowest_ll_rect(width, height, levels, &cursor->rect);
    if (status != DIC_STATUS_OK) return status;
    if (cursor->rect.width == 0 || cursor->rect.height == 0) {
        cursor->phase = CODEC_SCAN_PHASE_HIGH_PASS;
        return codec_scan_cursor_load_high_pass(cursor);
    }
    return DIC_STATUS_OK;
}

/**
 * @brief Returns one scan position and advances the state machine.
 *
 * A successful call sets has_position to one while coefficients remain. At
 * DONE it sets has_position to zero and leaves position unused.
 */
static dic_status codec_scan_cursor_next(codec_scan_cursor* cursor,
                                         codec_scan_position* position,
                                         int* has_position) {
    dic_status status;

    if (cursor == NULL || position == NULL || has_position == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    while (cursor->phase != CODEC_SCAN_PHASE_DONE) {
        if (cursor->y < cursor->rect.height) {
            position->local_x = cursor->x;
            position->local_y = cursor->y;
            position->index =
                codec_scan_index(cursor->width, cursor->rect.x + cursor->x,
                                 cursor->rect.y + cursor->y);
            position->is_ll = cursor->phase == CODEC_SCAN_PHASE_LL;
            position->level = position->is_ll ? 0 : cursor->level;
            position->orientation = position->is_ll
                                        ? DIC_SUBBAND_LL
                                        : codec_scan_orientations[cursor->band];

            cursor->x++;
            if (cursor->x >= cursor->rect.width) {
                cursor->x = 0;
                cursor->y++;
            }
            *has_position = 1;
            return DIC_STATUS_OK;
        }

        if (cursor->phase == CODEC_SCAN_PHASE_LL) {
            cursor->phase = CODEC_SCAN_PHASE_HIGH_PASS;
            cursor->level = cursor->levels;
            cursor->band = 0;
        } else {
            ++cursor->band;
        }

        status = codec_scan_cursor_load_high_pass(cursor);
        if (status != DIC_STATUS_OK) return status;
    }

    *has_position = 0;
    return DIC_STATUS_OK;
}

typedef struct codec_scan_index_buffer {
    size_t* indices;
    size_t count;
    size_t capacity;
} codec_scan_index_buffer;

DEFINE_VEC(codec_scan_index_buffer, codec_scan_index_buffer, size_t, indices,
           256u)

/**
 * @brief Checks descendants once and marks them only when they form a ZTR.
 *
 * The traversal appends descendant indices after their children, so the scratch
 * list is built in postorder. Recursive calls collect candidates but defer the
 * actual visited writes until the top-level subtree is known to be entirely
 * insignificant; otherwise the scratch tail is discarded.
 *
 * @note codec_scan_mark_descendants_visited are used to mark the descendants in
 * decoding cases.
 */
static dic_status codec_scan_mark_descendants_if_insignificant(
    const int32_t* plane, unsigned char* visited, int width, int height,
    int level, codec_subband_orientation orientation, int local_x, int local_y,
    int32_t threshold, codec_scan_index_buffer* scratch, int commit,
    int* is_ztr) {
    dic_rect_i32 child_rect;
    dic_status status;

    if (plane == NULL || visited == NULL || scratch == NULL || is_ztr == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    size_t mark_start = scratch->count;
    *is_ztr = 1;

    if (level <= 1) return DIC_STATUS_OK;

    status = codec_subband_rect(width, height, level, level - 1, orientation,
                                &child_rect);
    if (status != DIC_STATUS_OK) {
        *is_ztr = 0;
        return DIC_STATUS_OK;
    }

    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            int child_x = (local_x * 2) + dx;
            int child_y = (local_y * 2) + dy;
            int image_x, image_y;
            size_t child_index;
            int child_is_ztr;
            int32_t child_val;

            if (child_x >= child_rect.width || child_y >= child_rect.height)
                continue;

            image_x = child_rect.x + child_x;
            image_y = child_rect.y + child_y;
            child_index = codec_scan_index(width, image_x, image_y);
            child_val = plane[child_index];

            if (child_val >= threshold || child_val <= -threshold) {
                scratch->count = mark_start;
                *is_ztr = 0;
                return DIC_STATUS_OK;
            }

            status = codec_scan_mark_descendants_if_insignificant(
                plane, visited, width, height, level - 1, orientation, child_x,
                child_y, threshold, scratch, 0, &child_is_ztr);
            if (status != DIC_STATUS_OK) {
                scratch->count = mark_start;
                return status;
            }
            if (!child_is_ztr) {
                scratch->count = mark_start;
                *is_ztr = 0;
                return DIC_STATUS_OK;
            }

            status = codec_scan_index_buffer_append(scratch, child_index);
            if (status != DIC_STATUS_OK) {
                scratch->count = mark_start;
                return status;
            }
        }
    }

    if (commit) {
        for (size_t i = mark_start; i < scratch->count; ++i)
            visited[scratch->indices[i]] = 1u;
        scratch->count = mark_start;
    }

    return DIC_STATUS_OK;
}

/** @brief For ZTR, since ZTR has checked, all of the descendants are marked as
 * visited.
 *
 *  @note use codec_scan_mark_descendants_if_insignificant for it can detect ZTR
 * also mark visted.
 * */
static void codec_scan_mark_descendants_visited(
    unsigned char* visited, int width, int height, int level,
    codec_subband_orientation orientation, int local_x, int local_y) {
    dic_rect_i32 child_rect;

    if (level <= 1) return;

    if (codec_subband_rect(width, height, level, level - 1, orientation,
                           &child_rect) != DIC_STATUS_OK)
        return;

    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            int child_x = (local_x * 2) + dx;
            int child_y = (local_y * 2) + dy;
            int image_x, image_y;

            if (child_x >= child_rect.width || child_y >= child_rect.height)
                continue;

            image_x = child_rect.x + child_x;
            image_y = child_rect.y + child_y;
            visited[codec_scan_index(width, image_x, image_y)] = 1u;
            codec_scan_mark_descendants_visited(visited, width, height,
                                                level - 1, orientation, child_x,
                                                child_y);
        }
    }
}

/*  Token buffer                                                              */

/** @brief Growable uncompressed dominant-token sequence. */
typedef struct codec_scan_token_buffer {
    /** Heap array of @ref codec_scan_token values. */
    unsigned char* tokens;
    /** Number of valid tokens. */
    size_t count;
    /** Allocated token capacity. */
    size_t capacity;
} codec_scan_token_buffer;

DEFINE_VEC(codec_scan_token_buffer, codec_scan_token_buffer, unsigned char,
           tokens, 256u)

/*  Significance-order tracking                                               */

/**
 * @brief Dynamic discovery-order list of coefficients that became significant.
 */
typedef struct codec_scan_sig_order {
    /** Heap array of row-major coefficient indices. */
    size_t* indices;
    /** Number of valid indices. */
    size_t count;
    /** Allocated index capacity. */
    size_t capacity;
} codec_scan_sig_order;

DEFINE_VEC(codec_scan_sig_order, codec_scan_sig_order, size_t, indices, 256u)

static dic_hw2_huffman_tree codec_scan_fixed_tree;
static int codec_scan_fixed_tree_ready = 0;

/** Returns a pointer to the shared fixed Huffman tree, building it on first
 * call. */
static const dic_hw2_huffman_tree* codec_scan_get_fixed_tree(void) {
    if (!codec_scan_fixed_tree_ready) {
        dic_hw2_huffman_tree_init(&codec_scan_fixed_tree);
        /* Probabilities are known-good at compile time; ignore status. */
        (void)dic_hw2_huffman_build(codec_scan_fixed_probs,
                                    DIC_SCAN_TOKEN_COUNT,
                                    &codec_scan_fixed_tree);
        codec_scan_fixed_tree_ready = 1;
    }
    return &codec_scan_fixed_tree;
}

/*  Public table accessors                                                    */

void codec_scan_get_code_lengths(unsigned char lengths[DIC_SCAN_TOKEN_COUNT]) {
    const dic_hw2_huffman_tree* tree = codec_scan_get_fixed_tree();
    for (int i = 0; i < DIC_SCAN_TOKEN_COUNT; ++i)
        lengths[i] = (unsigned char)tree->codes[i].bit_length;
}

void codec_scan_set_code_lengths(
    const unsigned char lengths[DIC_SCAN_TOKEN_COUNT]) {
    double probs[DIC_SCAN_TOKEN_COUNT];

    /* p[i] = 2^(-L[i]); these satisfy Kraft equality for valid code-length
     * sets and produce the identical canonical tree via the standard builder.
     */
    for (int i = 0; i < DIC_SCAN_TOKEN_COUNT; ++i)
        probs[i] = 1.0 / (double)(1u << (unsigned)lengths[i]);

    if (codec_scan_fixed_tree_ready)
        dic_hw2_huffman_tree_free(&codec_scan_fixed_tree);
    dic_hw2_huffman_tree_init(&codec_scan_fixed_tree);
    (void)dic_hw2_huffman_build(probs, DIC_SCAN_TOKEN_COUNT,
                                &codec_scan_fixed_tree);
    codec_scan_fixed_tree_ready = 1;
}

/*  Huffman encode / decode wrappers */

static unsigned int codec_scan_map_token(const void* element) {
    return (unsigned int)(*(const unsigned char*)element);
}

static void codec_scan_write_token_fn(void* element, unsigned int symbol) {
    *(unsigned char*)element = (unsigned char)symbol;
}

static dic_status codec_scan_huffman_encode(
    const unsigned char* tokens, size_t token_count,
    dic_hw2_huffman_bitstream* bitstream) {
    const dic_hw2_huffman_tree* tree = codec_scan_get_fixed_tree();
    dic_hw2_huffman_bitstream_init(bitstream);
    return dic_hw2_huffman_encode_mapped(tree, tokens, token_count,
                                         sizeof(tokens[0]),
                                         codec_scan_map_token, bitstream);
}

static dic_status codec_scan_huffman_decode(
    const dic_hw2_huffman_bitstream* bitstream, unsigned char* tokens,
    size_t token_count) {
    const dic_hw2_huffman_tree* tree = codec_scan_get_fixed_tree();
    return dic_hw2_huffman_decode_mapped(tree, bitstream, token_count, tokens,
                                         sizeof(tokens[0]),
                                         codec_scan_write_token_fn);
}

/*  Significance pass encoder (one bitplane)                                  */

/**
 * @brief Emits dominant tokens for one threshold over the packed DWT plane.
 *
 * Scan order is lowest LL first, then high-pass bands from coarsest to finest
 * in HL, LH, HH order. ZTR suppresses all descendants of the current node for
 * this pass.
 */
static dic_status codec_scan_encode_significance_pass(
    int32_t* plane, int width, int height, int levels, int32_t threshold,
    unsigned char* significant, unsigned char* visited,
    codec_scan_token_buffer* tokens, codec_scan_sig_order* sig_order) {
    codec_scan_cursor cursor;
    codec_scan_position position;
    codec_scan_index_buffer ztr_marks;
    dic_status status;
    int has_position;

    status = codec_scan_cursor_init(&cursor, width, height, levels);
    if (status != DIC_STATUS_OK) return status;
    codec_scan_index_buffer_init(&ztr_marks);

    for (;;) {
        unsigned char token;

        // get next position in scan order
        status = codec_scan_cursor_next(&cursor, &position, &has_position);
        if (status != DIC_STATUS_OK || !has_position) break;

        size_t idx = position.index;
        if (significant[idx] || visited[idx]) continue;

        int32_t val = plane[idx];
        if (val >= threshold || val <= -threshold) {
            // first time significant, emit POS or NEG and
            // add idx to sig order
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

            if (!position.is_ll && position.level > 1) {
                status = codec_scan_mark_descendants_if_insignificant(
                    plane, visited, width, height, position.level,
                    position.orientation, position.local_x, position.local_y,
                    threshold, &ztr_marks, 1, &is_ztr);
                if (status != DIC_STATUS_OK) break;
            }

            // IZ have much bigger prob, so optimize for branch prediction
            if (!is_ztr) {
                token = (unsigned char)DIC_SCAN_TOKEN_IZ;
            } else {
                token = (unsigned char)DIC_SCAN_TOKEN_ZTR;
            }
        }

        if (status == DIC_STATUS_OK)
            status = codec_scan_token_buffer_append(tokens, token);
        if (status != DIC_STATUS_OK) break;
    }

    codec_scan_index_buffer_free(&ztr_marks);
    return status;
}

/*  Refinement pass encoder (one bitplane)                                    */

enum { CODEC_SCAN_ARITH_CONTEXTS = 3, CODEC_SCAN_ARITH_SCALE = 16384 };

/**
 * @brief Adaptive zero/one frequencies for refinement contexts.
 */
typedef struct codec_scan_arith_model {
    /** Observed zero frequencies per context. */
    uint32_t zero[CODEC_SCAN_ARITH_CONTEXTS];
    /** Observed one frequencies per context. */
    uint32_t one[CODEC_SCAN_ARITH_CONTEXTS];
} codec_scan_arith_model;

// init model with laplace smoothing
static void codec_scan_arith_model_init(codec_scan_arith_model* model) {
    for (int i = 0; i < CODEC_SCAN_ARITH_CONTEXTS; ++i) {
        model->zero[i] = 1u;
        model->one[i] = 1u;
    }
}

// scaling so freqs will not overflow
static void codec_scan_arith_model_update(codec_scan_arith_model* model,
                                          unsigned int context, int bit) {
    uint32_t total;
    if (bit)
        model->one[context]++;
    else
        model->zero[context]++;
    total = model->zero[context] + model->one[context];
    if (total >= CODEC_SCAN_ARITH_SCALE) {
        model->zero[context] = (model->zero[context] + 1u) / 2u;
        model->one[context] = (model->one[context] + 1u) / 2u;
    }
}

static int codec_scan_has_significant_neighbor(const unsigned char* significant,
                                               int width, int height,
                                               size_t index) {
    int x = (int)(index % (size_t)width);
    int y = (int)(index / (size_t)width);
    int dy, dx;
    for (dy = -1; dy <= 1; ++dy) {
        for (dx = -1; dx <= 1; ++dx) {
            int nx, ny;
            if (dx == 0 && dy == 0) continue;
            nx = x + dx;
            ny = y + dy;
            if (nx >= 0 && nx < width && ny >= 0 && ny < height &&
                significant[codec_scan_index(width, nx, ny)])
                return 1;
        }
    }
    return 0;
}

/**
 * @brief Arithmetic-codes refinement symbols with three adaptive contexts.
 *
 * Context zero has no significant neighbor, context one has one, and context
 * two is used after a coefficient has already received refinement history.
 * Output is written through the same LSB-first bit writer used by raw mode.
 */
static dic_status codec_scan_arithmetic_encode(const unsigned char* bits,
                                               const unsigned char* contexts,
                                               size_t count,
                                               bits_lsb_writer* writer) {
    const uint64_t top = 0xffffffffULL;
    const uint64_t half = 0x80000000ULL;
    const uint64_t first_qtr = 0x40000000ULL;
    const uint64_t third_qtr = 0xc0000000ULL;
    codec_scan_arith_model model;
    uint64_t low = 0u, high = top;
    // for underflow expansion
    size_t pending = 0u;

    if (count > (SIZE_MAX - 128u) / 16u) return DIC_STATUS_INVALID_ARGUMENT;
    size_t capacity = count * 16u + 128u;
    if (bits_lsb_writer_ensure_bits(writer, capacity) != DIC_STATUS_OK)
        return DIC_STATUS_MEMORY_ERROR;
    codec_scan_arith_model_init(&model);

    for (size_t i = 0u; i < count; i++) {
        unsigned int context = contexts[i];
        // for encoding each bits, use probs from context to split range
        uint32_t total = model.zero[context] + model.one[context];
        uint64_t range = high - low + 1u;
        uint64_t split = low + (range * model.zero[context]) / total;
        int bit = bits[i] != 0u;
        if (bit)
            low = split;
        else
            high = split - 1u;
        codec_scan_arith_model_update(&model, context, bit);

        for (;;) {
            int out = -1;
            if (high < half)
                out = 0;
            else if (low >= half) {
                out = 1;
                low -= half;
                high -= half;
            } else if (low >= first_qtr && high < third_qtr) {
                pending++;
                low -= first_qtr;
                high -= first_qtr;
            } else {
                break;
            }
            if (out >= 0) {
                bits_lsb_write(writer, out);
                //write pending num of opposite bits
                while (pending > 0u) {
                    bits_lsb_write(writer, !out);
                    pending--;
                }
            }
            low <<= 1u;
            high = (high << 1u) | 1u;
        }
    }
    pending++;
    {// deal with pending, after break
        int out = low < first_qtr ? 0 : 1;
        bits_lsb_write(writer, out);
        while (pending > 0u) {
            bits_lsb_write(writer, !out);
            pending--;
        }
    }
    bits_lsb_writer_flush(writer);
    return DIC_STATUS_OK;
}

/**
 * @brief Decodes the exact arithmetic refinement payload.
 */
static dic_status codec_scan_arithmetic_decode(const unsigned char* bytes,
                                               size_t bit_count,
                                               const unsigned char* contexts,
                                               size_t count,
                                               unsigned char* bits) {
    const uint64_t top = 0xffffffffULL;
    const uint64_t half = 0x80000000ULL;
    const uint64_t first_qtr = 0x40000000ULL;
    const uint64_t third_qtr = 0xc0000000ULL;
    codec_scan_arith_model model;
    bits_lsb_reader reader;
    uint64_t low = 0u, high = top, value = 0u;

    if (count > 0u && (bytes == NULL || bits == NULL || bit_count == 0u))
        return DIC_HW4_FORMAT_ERROR;
    codec_scan_arith_model_init(&model);
    bits_lsb_reader_init(&reader, bytes, bit_count);
    for (size_t i = 0u; i < 32u; ++i)
        value = (value << 1u) | (reader.bits_read < reader.bit_count
                                     ? (uint64_t)bits_lsb_read(&reader)
                                     : 0u);

    for (size_t i = 0u; i < count; i++) {
        unsigned int context = contexts[i];
        uint32_t total = model.zero[context] + model.one[context];
        uint64_t range = high - low + 1u;
        uint64_t split = low + (range * model.zero[context]) / total;
        int bit = value >= split;
        bits[i] = (unsigned char)bit;
        if (bit)
            low = split;
        else
            high = split - 1u;
        codec_scan_arith_model_update(&model, context, bit);

        for (;;) {
            if (high < half) {
            } else if (low >= half) {
                value -= half;
                low -= half;
                high -= half;
            } else if (low >= first_qtr && high < third_qtr) {
                value -= first_qtr;
                low -= first_qtr;
                high -= first_qtr;
            } else {
                break;
            }
            low <<= 1u;
            high = (high << 1u) | 1u;
            value <<= 1u;
            if (reader.bits_read < reader.bit_count)
                value |= (uint64_t)bits_lsb_read(&reader);
        }
    }
    return DIC_STATUS_OK;
}

/**
 * @brief Builds the subordinate payload for coefficients significant earlier.
 *
 * Arithmetic mode is retained only when it is strictly smaller than the raw
 * one-bit-per-symbol representation.
 * 
 * @param plane The DWT plane being encoded
 * @param bp The bitplane index being encoded
 * @param sig_order The order in which coefficients became significant the first time
 * @param refinement_count The number of coefficients to refine
 */
static dic_status codec_scan_encode_refinement_pass(
    const int32_t* plane, int width, int height, int bp,
    const codec_scan_sig_order* sig_order, size_t refinement_count,
    const unsigned char* significant, unsigned char* refinement_age,
    codec_scan_bitplane* bitplane) {
    unsigned char* bits = NULL;
    unsigned char* contexts = NULL;
    bits_lsb_writer arithmetic;
    dic_status status;

    bitplane->subordinate_symbol_count = refinement_count;
    if (refinement_count == 0u) return DIC_STATUS_OK;
    bits = (unsigned char*)malloc(refinement_count);
    contexts = (unsigned char*)malloc(refinement_count);
    if (bits == NULL || contexts == NULL) {
        free(bits);
        free(contexts);
        return DIC_STATUS_MEMORY_ERROR;
    }
    for (size_t i = 0u; i < refinement_count; ++i) {
        size_t idx = sig_order->indices[i];
        uint32_t mag = plane[idx] < 0 ? (uint32_t)(-(plane[idx] + 1)) + 1u
                                      : (uint32_t)plane[idx];
        // check if the old significant bit was 1 or 0,
        // and set the **context** for CABAC: 1. no sig around; 2. sig around; 3. already refined before
        bits[i] = (unsigned char)((mag >> bp) & 1u);
        // context = 0 just significant area the second high bit is more possible to be 0;
        //         = 1 just significant area the second high bit is has equal probs of 0/1;
        //         = 2 refined before, so simple cant predict;
        contexts[i] = refinement_age[idx] > 0u
                          ? 2u
                          : (unsigned char)(codec_scan_has_significant_neighbor(
                                                significant, width, height, idx)
                                                ? 1u
                                                : 0u);
        // age means this sig had been refined before
        if (refinement_age[idx] < 255u) ++refinement_age[idx];
    }

    // compress with arithmetic coding if it can save space, otherwise keep raw
    bits_lsb_writer_init(&arithmetic);
    status = codec_scan_arithmetic_encode(bits, contexts, refinement_count,
                                          &arithmetic);
    if (status == DIC_STATUS_OK && arithmetic.bit_count < refinement_count) {
        bitplane->subordinate_bits = arithmetic.bytes;
        bitplane->subordinate_bit_count = arithmetic.bit_count;
        bitplane->subordinate_byte_count = (arithmetic.bit_count + 7u) / 8u;
        bitplane->subordinate_mode = DIC_SCAN_REFINEMENT_ARITHMETIC;
    } else {
        bits_lsb_writer raw;
        free(arithmetic.bytes);
        bits_lsb_writer_init(&raw);
        status = bits_lsb_writer_ensure_bits(&raw, refinement_count);
        if (status == DIC_STATUS_OK) {
            for (size_t i = 0u; i < refinement_count; i++)
                bits_lsb_write(&raw, bits[i]);
            bits_lsb_writer_flush(&raw);
            bitplane->subordinate_bits = raw.bytes;
            bitplane->subordinate_bit_count = refinement_count;
            bitplane->subordinate_byte_count = (refinement_count + 7u) / 8u;
            bitplane->subordinate_mode = DIC_SCAN_REFINEMENT_RAW;
        }
    }
    free(bits);
    free(contexts);
    return status;
}

/*  Refinement pass decoder (one bitplane)                                    */

/**
 * @brief Decodes and validates one subordinate refinement payload.
 *
 * Arithmetic payloads are re-encoded canonically and byte-compared so damaged
 * or non-canonical data cannot silently pass.
 */
static dic_status codec_scan_decode_refinement_pass(
    const codec_scan_bitplane* bitplane, int width, int height, int bp,
    int32_t* plane, const unsigned char* significant,
    unsigned char* refinement_age, const size_t* sig_order,
    size_t refinement_count) {
    unsigned char* bits = NULL;
    unsigned char* contexts = NULL;
    size_t i;
    dic_status status = DIC_STATUS_OK;
    if (bitplane->subordinate_symbol_count != refinement_count)
        return DIC_HW4_FORMAT_ERROR;
    if (refinement_count == 0u)
        return bitplane->subordinate_bit_count == 0u ? DIC_STATUS_OK
                                                     : DIC_HW4_FORMAT_ERROR;
    bits = (unsigned char*)malloc(refinement_count);
    contexts = (unsigned char*)malloc(refinement_count);
    if (bits == NULL || contexts == NULL) {
        free(bits);
        free(contexts);
        return DIC_STATUS_MEMORY_ERROR;
    }
    for (i = 0; i < refinement_count; ++i) {
        size_t idx = sig_order[i];
        contexts[i] = refinement_age[idx] > 0u
                          ? 2u
                          : (unsigned char)(codec_scan_has_significant_neighbor(
                                                significant, width, height, idx)
                                                ? 1u
                                                : 0u);
    }
    if (bitplane->subordinate_mode == DIC_SCAN_REFINEMENT_RAW) {
        bits_lsb_reader reader;
        if (bitplane->subordinate_bit_count != refinement_count) {
            status = DIC_HW4_FORMAT_ERROR;
            goto cleanup;
        }
        bits_lsb_reader_init(&reader, bitplane->subordinate_bits,
                             bitplane->subordinate_bit_count);
        for (i = 0u; i < refinement_count; ++i)
            bits[i] = (unsigned char)bits_lsb_read(&reader);
    } else if (bitplane->subordinate_mode == DIC_SCAN_REFINEMENT_ARITHMETIC) {
        bits_lsb_writer canonical;
        status = codec_scan_arithmetic_decode(bitplane->subordinate_bits,
                                              bitplane->subordinate_bit_count,
                                              contexts, refinement_count, bits);
        if (status != DIC_STATUS_OK) goto cleanup;
        bits_lsb_writer_init(&canonical);
        status = codec_scan_arithmetic_encode(bits, contexts, refinement_count,
                                              &canonical);
        if (status == DIC_STATUS_OK &&
            (canonical.bit_count != bitplane->subordinate_bit_count ||
             (canonical.bit_count + 7u) / 8u !=
                 bitplane->subordinate_byte_count ||
             memcmp(canonical.bytes, bitplane->subordinate_bits,
                    (canonical.bit_count + 7u) / 8u) != 0))
            status = DIC_HW4_FORMAT_ERROR;
        free(canonical.bytes);
        if (status != DIC_STATUS_OK) goto cleanup;
    } else {
        status = DIC_HW4_FORMAT_ERROR;
        goto cleanup;
    }
    for (i = 0; i < refinement_count; ++i) {
        size_t idx = sig_order[i];
        int32_t val = plane[idx];
        uint32_t mag = val < 0 ? (uint32_t)(-(val + 1)) + 1u : (uint32_t)val;
        if (bits[i]) mag |= ((uint32_t)1u << (unsigned)bp);
        plane[idx] = (val < 0) ? -(int32_t)mag : (int32_t)mag;
        if (refinement_age[idx] < 255u) ++refinement_age[idx];
    }
cleanup:
    free(bits);
    free(contexts);
    return status;
}

void codec_scan_bitplane_init(codec_scan_bitplane* bp) {
    int symbol;
    if (bp == NULL) return;
    bp->dominant_token_count = 0u;
    for (symbol = 0; symbol < DIC_SCAN_TOKEN_COUNT; ++symbol)
        bp->dominant_symbol_counts[symbol] = 0u;
    dic_hw2_huffman_bitstream_init(&bp->dominant_stream);
    bp->subordinate_bits = NULL;
    bp->subordinate_symbol_count = 0u;
    bp->subordinate_bit_count = 0u;
    bp->subordinate_byte_count = 0u;
    bp->subordinate_mode = DIC_SCAN_REFINEMENT_RAW;
}

void codec_scan_bitplane_free(codec_scan_bitplane* bp) {
    if (bp == NULL) return;
    dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
    free(bp->subordinate_bits);
    codec_scan_bitplane_init(bp);
}

unsigned char codec_scan_amplitude_size(int32_t amplitude) {
    uint32_t magnitude;
    unsigned char bits = 0u;
    if (amplitude == 0) return 0u;
    magnitude =
        amplitude < 0 ? (uint32_t)(-(amplitude + 1)) + 1u : (uint32_t)amplitude;
    while (magnitude != 0u) {
        ++bits;
        magnitude >>= 1;
    }
    return bits;
}

dic_status codec_scan_encode_plane(const int32_t* plane, int width, int height,
                                   int levels,
                                   codec_scan_bitplane** bitplanes_out,
                                   int* bitplane_count_out) {
    // codec_scan_bitplane[] encoded bitplane
    codec_scan_bitplane* bps = NULL;
    unsigned char* significant = NULL;
    unsigned char* refinement_age = NULL;
    dic_status status = DIC_STATUS_OK;
    int32_t* work_plane = NULL;

    if (plane == NULL || bitplanes_out == NULL || bitplane_count_out == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    *bitplanes_out = NULL;
    *bitplane_count_out = 0;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK) return status;

    // total pixel in one plane
    size_t plane_count = (size_t)width * (size_t)height;

    // caclulate the bitplane by searching the maximun magntitude
    // floor(log2(max(abs(plane[i]))))
    int max_bp = -1;
    uint32_t max_mag = 0u;
    for (size_t i = 0; i < plane_count; ++i) {
        // abs(INT32_MIN) > int32_t max
        uint32_t mag = plane[i] < 0 ? (uint32_t)(-(plane[i] + 1)) + 1u
                                    : (uint32_t)plane[i];
        if (mag > max_mag) max_mag = mag;
    }
    while (max_mag > 0u) {
        max_bp++;
        max_mag >>= 1;
    }
    if (max_bp < 0) {
        /* An all-zero plane requires no bit-plane payload. */
        return DIC_STATUS_OK;
    }

    int total_bp = max_bp + 1;
    bps = (codec_scan_bitplane*)calloc((size_t)total_bp, sizeof(bps[0]));
    if (bps == NULL) return DIC_STATUS_MEMORY_ERROR;

    // create working copy, so EZW will not work on the origin plane
    work_plane = (int32_t*)malloc(plane_count * sizeof(work_plane[0]));
    if (work_plane == NULL) {
        free(bps);
        return DIC_STATUS_MEMORY_ERROR;
    }
    memcpy(work_plane, plane, plane_count * sizeof(plane[0]));

    // significant and refinement state
    // change by codec_scan_encode_significance_pass
    // when the pixel is significant
    significant = (unsigned char*)calloc(plane_count, 1u);
    if (significant == NULL) {
        free(work_plane);
        free(bps);
        return DIC_STATUS_MEMORY_ERROR;
    }
    refinement_age = (unsigned char*)calloc(plane_count, 1u);
    if (refinement_age == NULL) {
        free(significant);
        free(work_plane);
        free(bps);
        return DIC_STATUS_MEMORY_ERROR;
    }

    // hold the order of pixel become significant at first time
    // for refinement pass to know which pixel need to be refined
    codec_scan_sig_order sig_order;
    codec_scan_sig_order_init(&sig_order);

    /*
     * One output element is produced per threshold:
     *
     *   bp=max_bp  threshold=2^max_bp  -> output[0]
     *   bp=max_bp-1                    -> output[1]
     *   ...
     *   bp=0                           -> output[max_bp]
     */
    for (int bp = max_bp; bp >= 0; bp--) {
        int32_t threshold = (int32_t)(1u << (unsigned)bp);
        codec_scan_token_buffer token_buf;
        // write by codec_scan_mark_descendants_visited
        // mark ZTR passed pixel
        unsigned char* visited = NULL;
        size_t sig_before = sig_order.count;
        // tweek to next bitplane
        codec_scan_bitplane* cur = bps + (max_bp - bp); /* MSB at index 0 */

        visited = (unsigned char*)calloc(plane_count, 1u);
        if (visited == NULL) {
            status = DIC_STATUS_MEMORY_ERROR;
            break;
        }
        codec_scan_token_buffer_init(&token_buf);

        /* Significance pass */
        status = codec_scan_encode_significance_pass(
            work_plane, width, height, levels, threshold, significant, visited,
            &token_buf, &sig_order);
        free(visited);
        if (status != DIC_STATUS_OK) {
            codec_scan_token_buffer_free(&token_buf);
            break;
        }

        // For those already significant in other bitplanes,
        // do refinement pass to compress
        status = codec_scan_encode_refinement_pass(
            work_plane, width, height, bp, &sig_order, sig_before, significant,
            refinement_age, cur);

        if (status != DIC_STATUS_OK) {
            codec_scan_token_buffer_free(&token_buf);
            break;
        }

        cur->dominant_token_count = token_buf.count;
        {
            size_t token_index;
            for (token_index = 0u; token_index < token_buf.count;
                 ++token_index) {
                unsigned int symbol = token_buf.tokens[token_index];
                if (symbol < DIC_SCAN_TOKEN_COUNT)
                    ++cur->dominant_symbol_counts[symbol];
            }
        }

        /* Huffman-encode dominant tokens with the fixed table. */
        status = codec_scan_huffman_encode(token_buf.tokens, token_buf.count,
                                           &cur->dominant_stream);

        codec_scan_token_buffer_free(&token_buf);
        if (status != DIC_STATUS_OK) break;
    }

    codec_scan_sig_order_free(&sig_order);
    free(refinement_age);
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

dic_status codec_scan_decode_plane(const codec_scan_bitplane* bitplanes,
                                   int total_bitplane_count, int num_bitplanes,
                                   int width, int height, int levels,
                                   int32_t* plane) {
    size_t plane_count;
    unsigned char* significant = NULL;
    unsigned char* refinement_age = NULL;
    codec_scan_sig_order sig_order;
    dic_status status;
    int bp_idx;

    if (bitplanes == NULL || plane == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    if (num_bitplanes <= 0) return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK) return status;

    /*
     * Reconstruct from the same MSB-first prefix:
     *
     * bitplanes[0] --------------------------> threshold 2^(total-1)
     * bitplanes[num_bitplanes - 1] ----------> lowest available threshold
     * omitted suffix ------------------------> remains zero
     */
    plane_count = (size_t)width * (size_t)height;
    memset(plane, 0, plane_count * sizeof(plane[0]));

    significant = (unsigned char*)calloc(plane_count, 1u);
    if (significant == NULL) return DIC_STATUS_MEMORY_ERROR;
    refinement_age = (unsigned char*)calloc(plane_count, 1u);
    if (refinement_age == NULL) {
        free(significant);
        return DIC_STATUS_MEMORY_ERROR;
    }

    codec_scan_sig_order_init(&sig_order);

    for (bp_idx = 0; bp_idx < num_bitplanes; ++bp_idx) {
        const codec_scan_bitplane* cur = bitplanes + bp_idx;
        /* The first bitplane is MSB (max_bp). We don't need the absolute bp
         * value for decoding; the reconstruction is additive. Each bitplane
         * contributes its threshold to newly-significant coefficients.
         * We compute threshold from bp_idx and global max_bp: since we no
         * longer store max_bp explicitly in the bitplane struct, we compute it
         * from the total count. */
        int bp = total_bitplane_count - 1 - bp_idx;
        int32_t threshold = (int32_t)(1u << (unsigned)bp);
        unsigned char* tokens = NULL;
        unsigned char* visited = NULL;
        size_t token_offset = 0u;
        size_t sig_before = sig_order.count;

        tokens = cur->dominant_token_count == 0u
                     ? NULL
                     : (unsigned char*)malloc(cur->dominant_token_count);
        if (tokens == NULL && cur->dominant_token_count > 0u) {
            status = DIC_STATUS_MEMORY_ERROR;
            goto cleanup;
        }
        status = codec_scan_huffman_decode(&cur->dominant_stream, tokens,
                                           cur->dominant_token_count);
        if (status != DIC_STATUS_OK) {
            free(tokens);
            goto cleanup;
        }

        visited = (unsigned char*)calloc(plane_count, 1u);
        if (visited == NULL) {
            free(tokens);
            status = DIC_STATUS_MEMORY_ERROR;
            goto cleanup;
        }

        /* Significance pass decode */
        {
            codec_scan_cursor cursor;
            codec_scan_position position;
            int has_position;

            status = codec_scan_cursor_init(&cursor, width, height, levels);
            while (status == DIC_STATUS_OK) {
                size_t idx;
                unsigned char token;

                status =
                    codec_scan_cursor_next(&cursor, &position, &has_position);
                if (status != DIC_STATUS_OK || !has_position) break;

                idx = position.index;
                if (significant[idx] || visited[idx]) continue;
                if (token_offset >= cur->dominant_token_count) {
                    status = DIC_HW4_FORMAT_ERROR;
                    break;
                }

                token = tokens[token_offset++];
                if (token == (unsigned char)DIC_SCAN_TOKEN_POS) {
                    plane[idx] = threshold;
                    significant[idx] = 1u;
                    status = codec_scan_sig_order_append(&sig_order, idx);
                } else if (token == (unsigned char)DIC_SCAN_TOKEN_NEG) {
                    plane[idx] = -threshold;
                    significant[idx] = 1u;
                    status = codec_scan_sig_order_append(&sig_order, idx);
                } else if (token == (unsigned char)DIC_SCAN_TOKEN_ZTR &&
                           !position.is_ll) {
                    visited[idx] = 1u;
                    codec_scan_mark_descendants_visited(
                        visited, width, height, position.level,
                        position.orientation, position.local_x,
                        position.local_y);
                } else if (token != (unsigned char)DIC_SCAN_TOKEN_IZ) {
                    status = DIC_HW4_FORMAT_ERROR;
                }
            }
        }

        free(tokens);
        free(visited);
        if (status != DIC_STATUS_OK) goto cleanup;
        if (token_offset != cur->dominant_token_count) {
            status = DIC_HW4_FORMAT_ERROR;
            goto cleanup;
        }

        status = codec_scan_decode_refinement_pass(
            cur, width, height, bp, plane, significant, refinement_age,
            sig_order.indices, sig_before);
        if (status != DIC_STATUS_OK) goto cleanup;
    }

    /* Midpoint reconstruction when decoding fewer than all bitplanes */
    if (num_bitplanes >= total_bitplane_count) goto cleanup;
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

    status = DIC_STATUS_OK;

cleanup:
    codec_scan_sig_order_free(&sig_order);
    free(refinement_age);
    free(significant);
    return status;
}

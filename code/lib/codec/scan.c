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

#include "codec/subband.h"
#include "wavelet/dic_dwt53.h"


inline static size_t codec_scan_index(int width, int x, int y) {
  return ((size_t)y * (size_t)width) + (size_t)x;
}

static int codec_scan_find_max_bitplane(const int32_t* plane, size_t count) {
  uint32_t max_mag = 0u;
  size_t i;

  for (i = 0; i < count; ++i) {
    uint32_t mag =
        plane[i] < 0 ? (uint32_t)(-(plane[i] + 1)) + 1u : (uint32_t)plane[i];
    if (mag > max_mag) max_mag = mag;
  }

  if (max_mag == 0u) return -1;

  {
    int bp = -1;
    while (max_mag > 0u) {
      ++bp;
      max_mag >>= 1;
    }
    return bp;
  }
}

static int codec_scan_descendants_insignificant(
    const int32_t* plane, int width, int height, int level,
    codec_subband_orientation orientation, int local_x, int local_y,
    int32_t threshold) {
  dic_rect_i32 child_rect;
  int dx, dy;

  if (level <= 1) return 1;

  if (codec_subband_rect(width, height, level, level - 1, orientation,
                         &child_rect) != DIC_STATUS_OK)
    return 0;

  for (dy = 0; dy < 2; ++dy) {
    for (dx = 0; dx < 2; ++dx) {
      int child_x = (local_x * 2) + dx;
      int child_y = (local_y * 2) + dy;
      int image_x, image_y;
      int32_t child_val;

      if (child_x >= child_rect.width || child_y >= child_rect.height) continue;

      image_x = child_rect.x + child_x;
      image_y = child_rect.y + child_y;
      child_val = plane[codec_scan_index(width, image_x, image_y)];

      if (child_val >= threshold || child_val <= -threshold) return 0;

      if (!codec_scan_descendants_insignificant(plane, width, height, level - 1,
                                                orientation, child_x, child_y,
                                                threshold))
        return 0;
    }
  }

  return 1;
}

static void codec_scan_mark_descendants_visited(
    unsigned char* visited, int width, int height, int level,
    codec_subband_orientation orientation, int local_x, int local_y) {
  dic_rect_i32 child_rect;
  int dx, dy;

  if (level <= 1) return;

  if (codec_subband_rect(width, height, level, level - 1, orientation,
                         &child_rect) != DIC_STATUS_OK)
    return;

  for (dy = 0; dy < 2; ++dy) {
    for (dx = 0; dx < 2; ++dx) {
      int child_x = (local_x * 2) + dx;
      int child_y = (local_y * 2) + dy;
      int image_x, image_y;

      if (child_x >= child_rect.width || child_y >= child_rect.height) continue;

      image_x = child_rect.x + child_x;
      image_y = child_rect.y + child_y;
      visited[codec_scan_index(width, image_x, image_y)] = 1u;
      codec_scan_mark_descendants_visited(visited, width, height, level - 1,
                                          orientation, child_x, child_y);
    }
  }
}

/*  Bit-level I/O                                                             */

/**
 * @brief Growable LSB-first bit accumulator.
 *
 * `current_byte` holds the not-yet-flushed suffix. `bit_count` includes both
 * flushed bytes and that suffix.
 */
typedef struct codec_scan_bit_writer {
  /** Heap storage containing completed bytes and final partial byte. */
  unsigned char* bytes;
  /** Allocated byte capacity of @ref bytes. */
  size_t byte_capacity;
  /** Total meaningful bits appended. */
  size_t bit_count;
  /** Partial byte not yet copied to @ref bytes. */
  unsigned char current_byte;
  /** Next bit index in @ref current_byte, from zero through seven. */
  int bit_pos;
} codec_scan_bit_writer;

/** @brief Resets a writer without allocating storage. */
static void codec_scan_bit_writer_init(codec_scan_bit_writer* w) {
  if (w == NULL) return;
  w->bytes = NULL;
  w->byte_capacity = 0u;
  w->bit_count = 0u;
  w->current_byte = 0u;
  w->bit_pos = 0;
}

/** @brief Ensures capacity for a total number of logical bits. */
static dic_status codec_scan_bit_writer_ensure(size_t total_bits,
                                               codec_scan_bit_writer* w) {
  size_t needed = (total_bits + 7u) / 8u;
  if (w == NULL) return DIC_STATUS_INVALID_ARGUMENT;
  if (needed <= w->byte_capacity) return DIC_STATUS_OK;
  {
    size_t cap = w->byte_capacity == 0u ? 64u : w->byte_capacity;
    while (cap < needed) {
      if (cap > SIZE_MAX / 2u) {
        cap = needed;
        break;
      }
      cap *= 2u;
    }
    {
      unsigned char* tmp = (unsigned char*)realloc(w->bytes, cap);
      if (tmp == NULL) return DIC_STATUS_MEMORY_ERROR;
      w->bytes = tmp;
      w->byte_capacity = cap;
    }
  }
  return DIC_STATUS_OK;
}

/** @brief Appends one bit to current_byte at bit_pos 0 through 7. */
static void codec_scan_bit_write(int bit, codec_scan_bit_writer* w) {
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

/** @brief Materializes the final partially occupied byte. */
static void codec_scan_bit_writer_flush(codec_scan_bit_writer* w) {
  if (w == NULL || w->bit_pos == 0) return;
  w->bytes[w->bit_count / 8u] = w->current_byte;
  w->current_byte = 0u;
  w->bit_pos = 0;
}

/** @brief Bounded LSB-first view over a serialized bit payload. */
typedef struct codec_scan_bit_reader {
  /** Borrowed payload bytes. */
  const unsigned char* bytes;
  /** Number of addressable bytes derived from @ref bit_count. */
  size_t byte_count;
  /** Number of meaningful payload bits. */
  size_t bit_count;
  /** Number of logical bits already consumed. */
  size_t bits_read;
} codec_scan_bit_reader;

/** @brief Configures a reader for exactly @p bit_count meaningful bits. */
static void codec_scan_bit_reader_init(codec_scan_bit_reader* r,
                                       const unsigned char* bytes,
                                       size_t bit_count) {
  if (r == NULL) return;
  r->bytes = bytes;
  r->byte_count = (bit_count + 7u) / 8u;
  r->bit_count = bit_count;
  r->bits_read = 0u;
}

/** @brief Returns the next logical bit, or zero after the declared end. */
static int codec_scan_bit_read(codec_scan_bit_reader* r) {
  size_t byte_idx, bit_idx;
  if (r == NULL || r->bits_read >= r->bit_count) return 0;
  byte_idx = r->bits_read / 8u;
  bit_idx = r->bits_read % 8u;
  ++r->bits_read;
  return (int)((r->bytes[byte_idx] >> bit_idx) & 1u);
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

static void codec_scan_token_buffer_init(codec_scan_token_buffer* buf) {
  if (buf == NULL) return;
  buf->tokens = NULL;
  buf->count = 0u;
  buf->capacity = 0u;
}

static void codec_scan_token_buffer_free(codec_scan_token_buffer* buf) {
  if (buf == NULL) return;
  free(buf->tokens);
  codec_scan_token_buffer_init(buf);
}

static dic_status codec_scan_token_buffer_append(codec_scan_token_buffer* buf,
                                                 unsigned char token) {
  if (buf == NULL) return DIC_STATUS_INVALID_ARGUMENT;
  if (buf->count >= buf->capacity) {
    size_t cap = buf->capacity == 0u ? 256u : buf->capacity;
    while (cap <= buf->count) {
      if (cap > SIZE_MAX / 2u) {
        cap = buf->count + 1u;
        break;
      }
      cap *= 2u;
    }
    {
      unsigned char* tmp = (unsigned char*)realloc(buf->tokens, cap);
      if (tmp == NULL) return DIC_STATUS_MEMORY_ERROR;
      buf->tokens = tmp;
      buf->capacity = cap;
    }
  }
  buf->tokens[buf->count] = token;
  ++buf->count;
  return DIC_STATUS_OK;
}

/**
 * @brief Returns the unsigned Exp-Golomb code length for one run value.
 *
 * @code{.unparsed}
 * value  code_num  code
 *   0       1      1
 *   1       2      010
 *   2       3      011
 *   3       4      00100
 * @endcode
 */
static size_t codec_scan_ue_bit_count(size_t value) {
  size_t code_num = value + 1u;
  size_t width = 0u;
  while (code_num != 0u) {
    ++width;
    code_num >>= 1u;
  }
  return (width * 2u) - 1u;
}

static dic_status codec_scan_write_ue(codec_scan_bit_writer* writer,
                                      size_t value) {
  size_t code_num = value + 1u;
  size_t width = 0u;
  size_t i;
  size_t temp = code_num;
  while (temp != 0u) {
    ++width;
    temp >>= 1u;
  }
  if (codec_scan_bit_writer_ensure(writer->bit_count + width * 2u, writer) !=
      DIC_STATUS_OK)
    return DIC_STATUS_MEMORY_ERROR;
  for (i = 1u; i < width; ++i) codec_scan_bit_write(0, writer);
  for (i = width; i > 0u; --i)
    codec_scan_bit_write((int)((code_num >> (i - 1u)) & 1u), writer);
  return DIC_STATUS_OK;
}

static int codec_scan_read_ue(codec_scan_bit_reader* reader, size_t* value) {
  size_t zeros = 0u;
  size_t code_num = 1u;
  size_t i;
  int terminator_found = 0;
  if (reader == NULL || value == NULL) return 0;
  while (reader->bits_read < reader->bit_count) {
    if (codec_scan_bit_read(reader)) {
      terminator_found = 1;
      break;
    }
    if (++zeros >= sizeof(size_t) * 8u - 1u) return 0;
  }
  if (!terminator_found) return 0;
  for (i = 0u; i < zeros; ++i) {
    if (reader->bits_read >= reader->bit_count) return 0;
    code_num = (code_num << 1u) | (size_t)codec_scan_bit_read(reader);
  }
  *value = code_num - 1u;
  return 1;
}

/**
 * @brief Replaces profitable IZ runs with ZRUN plus an Exp-Golomb side value.
 *
 * A ZRUN represents `5 + ue_value` expanded IZ tokens. The command itself is
 * Huffman-coded; only ue_value is written to the run side stream.
 */
static dic_status codec_scan_rle_commands(
    const codec_scan_token_buffer* tokens, codec_scan_token_buffer* commands,
    codec_scan_bit_writer* runs) {
  size_t i = 0u;
  dic_status status = DIC_STATUS_OK;
  while (i < tokens->count) {
    if (tokens->tokens[i] == (unsigned char)DIC_SCAN_TOKEN_IZ) {
      size_t run = 1u;
      while (i + run < tokens->count &&
             tokens->tokens[i + run] == (unsigned char)DIC_SCAN_TOKEN_IZ)
        ++run;
      if (run >= 5u &&
          3u + codec_scan_ue_bit_count(run - 5u) < run) {
        status = codec_scan_token_buffer_append(
            commands, (unsigned char)DIC_SCAN_TOKEN_ZRUN);
        if (status == DIC_STATUS_OK)
          status = codec_scan_write_ue(runs, run - 5u);
      } else {
        size_t j;
        for (j = 0u; status == DIC_STATUS_OK && j < run; ++j)
          status = codec_scan_token_buffer_append(
              commands, (unsigned char)DIC_SCAN_TOKEN_IZ);
      }
      i += run;
    } else {
      status = codec_scan_token_buffer_append(commands, tokens->tokens[i++]);
    }
    if (status != DIC_STATUS_OK) return status;
  }
  codec_scan_bit_writer_flush(runs);
  return DIC_STATUS_OK;
}

/** @brief Inverts ZRUN commands and validates exact token/bit consumption. */
static dic_status codec_scan_expand_commands(
    const unsigned char* commands, size_t command_count,
    const unsigned char* run_bytes, size_t run_bits, size_t token_count,
    unsigned char** tokens_out) {
  codec_scan_bit_reader reader;
  unsigned char* tokens;
  size_t command;
  size_t produced = 0u;

  if (tokens_out == NULL) return DIC_STATUS_INVALID_ARGUMENT;
  *tokens_out = NULL;
  tokens = token_count == 0u ? NULL : (unsigned char*)malloc(token_count);
  if (tokens == NULL && token_count > 0u) return DIC_STATUS_MEMORY_ERROR;
  codec_scan_bit_reader_init(&reader, run_bytes, run_bits);

  for (command = 0u; command < command_count; ++command) {
    unsigned char token = commands[command];
    if (token == (unsigned char)DIC_SCAN_TOKEN_ZRUN) {
      size_t value;
      size_t run;
      if (!codec_scan_read_ue(&reader, &value) || value > SIZE_MAX - 5u) {
        free(tokens);
        return DIC_HW4_FORMAT_ERROR;
      }
      run = value + 5u;
      if (run > token_count - produced) {
        free(tokens);
        return DIC_HW4_FORMAT_ERROR;
      }
      memset(tokens + produced, DIC_SCAN_TOKEN_IZ, run);
      produced += run;
    } else {
      if (token >= (unsigned char)DIC_SCAN_TOKEN_ZRUN ||
          produced >= token_count) {
        free(tokens);
        return DIC_HW4_FORMAT_ERROR;
      }
      tokens[produced++] = token;
    }
  }
  if (produced != token_count || reader.bits_read != reader.bit_count) {
    free(tokens);
    return DIC_HW4_FORMAT_ERROR;
  }
  *tokens_out = tokens;
  return DIC_STATUS_OK;
}

/*  Significance-order tracking                                               */

/**
 * @brief Discovery-order list of coefficients that became significant.
 */
typedef struct codec_scan_sig_order {
  /** Heap array of row-major coefficient indices. */
  size_t* indices;
  /** Number of valid indices. */
  size_t count;
  /** Allocated index capacity. */
  size_t capacity;
} codec_scan_sig_order;

static void codec_scan_sig_order_init(codec_scan_sig_order* order) {
  if (order == NULL) return;
  order->indices = NULL;
  order->count = 0u;
  order->capacity = 0u;
}

static void codec_scan_sig_order_free(codec_scan_sig_order* order) {
  if (order == NULL) return;
  free(order->indices);
  codec_scan_sig_order_init(order);
}

static dic_status codec_scan_sig_order_append(codec_scan_sig_order* order,
                                              size_t index) {
  if (order == NULL) return DIC_STATUS_INVALID_ARGUMENT;
  if (order->count >= order->capacity) {
    size_t cap = order->capacity == 0u ? 256u : order->capacity;
    while (cap <= order->count) {
      if (cap > SIZE_MAX / 2u) {
        cap = order->count + 1u;
        break;
      }
      cap *= 2u;
    }
    {
      size_t* tmp =
          (size_t*)realloc(order->indices, cap * sizeof(order->indices[0]));
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
/*  Fixed Huffman table (JPEG Annex K style)                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Fixed token probabilities derived from expected EZW statistics.
 *
 * IZ (Isolated Zero) dominates because most coefficients are insignificant
 * at any given bitplane.  ZTR (Zerotree Root) appears only in full-plane mode
 * (~8%).  POS and NEG are symmetric rare events (~6% each).
 *
 * These probabilities are used to build a single shared Huffman tree at init
 * time, avoiding per-bitplane tree construction entirely.
 *
 * Each probability can be overridden at compile time by defining the
 * corresponding macro before including this translation unit, e.g.:
 *   -DCODEC_SCAN_PROB_IZ=0.75 -DCODEC_SCAN_PROB_POS=0.10
 */

static const double codec_scan_fixed_probs[DIC_SCAN_TOKEN_COUNT] = {
    0.50,  /* IZ */
    0.125, /* ZTR */
    0.125, /* POS */
    0.125, /* NEG */
    0.125  /* ZRUN */
};

static dic_hw2_huffman_tree codec_scan_fixed_tree;
static int codec_scan_fixed_tree_ready = 0;

/** Returns a pointer to the shared fixed Huffman tree, building it on first
 * call. */
static const dic_hw2_huffman_tree* codec_scan_get_fixed_tree(void) {
  if (!codec_scan_fixed_tree_ready) {
    dic_hw2_huffman_tree_init(&codec_scan_fixed_tree);
    /* Probabilities are known-good at compile time; ignore status. */
    (void)dic_hw2_huffman_build(codec_scan_fixed_probs, DIC_SCAN_TOKEN_COUNT,
                                &codec_scan_fixed_tree);
    codec_scan_fixed_tree_ready = 1;
  }
  return &codec_scan_fixed_tree;
}

/*  Public table accessors                                                    */

void codec_scan_get_code_lengths(unsigned char lengths[DIC_SCAN_TOKEN_COUNT]) {
  const dic_hw2_huffman_tree* tree = codec_scan_get_fixed_tree();
  int i;
  for (i = 0; i < DIC_SCAN_TOKEN_COUNT; ++i)
    lengths[i] = (unsigned char)tree->codes[i].bit_length;
}

void codec_scan_set_code_lengths(
    const unsigned char lengths[DIC_SCAN_TOKEN_COUNT]) {
  double probs[DIC_SCAN_TOKEN_COUNT];
  int i;

  /* p[i] = 2^(-L[i]); these satisfy Kraft equality for valid code-length
   * sets and produce the identical canonical tree via the standard builder. */
  for (i = 0; i < DIC_SCAN_TOKEN_COUNT; ++i)
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
                                       sizeof(tokens[0]), codec_scan_map_token,
                                       bitstream);
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
  dic_rect_i32 ll_rect;
  dic_status status;
  int level;

  status = codec_subband_lowest_ll_rect(width, height, levels, &ll_rect);
  if (status != DIC_STATUS_OK) return status;

  /* LL subband: no zerotree descendants. */
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
    codec_subband_orientation orientations[3] = {DIC_SUBBAND_HL, DIC_SUBBAND_LH,
                                                 DIC_SUBBAND_HH};
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
              codec_scan_mark_descendants_visited(visited, width, height, level,
                                                  orient, x, y);
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

/*  Refinement pass encoder (one bitplane)                                    */

enum {
  CODEC_SCAN_ARITH_CONTEXTS = 3,
  CODEC_SCAN_ARITH_SCALE = 16384
};

/**
 * @brief Adaptive zero/one frequencies for refinement contexts.
 */
typedef struct codec_scan_arith_model {
  /** Observed zero frequencies per context. */
  uint32_t zero[CODEC_SCAN_ARITH_CONTEXTS];
  /** Observed one frequencies per context. */
  uint32_t one[CODEC_SCAN_ARITH_CONTEXTS];
} codec_scan_arith_model;

static void codec_scan_arith_model_init(codec_scan_arith_model* model) {
  int i;
  for (i = 0; i < CODEC_SCAN_ARITH_CONTEXTS; ++i) {
    model->zero[i] = 1u;
    model->one[i] = 1u;
  }
}

static void codec_scan_arith_model_update(codec_scan_arith_model* model,
                                          unsigned int context, int bit) {
  uint32_t total;
  if (bit)
    ++model->one[context];
  else
    ++model->zero[context];
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
static dic_status codec_scan_arithmetic_encode(
    const unsigned char* bits, const unsigned char* contexts, size_t count,
    codec_scan_bit_writer* writer) {
  const uint64_t top = 0xffffffffULL;
  const uint64_t half = 0x80000000ULL;
  const uint64_t first_qtr = 0x40000000ULL;
  const uint64_t third_qtr = 0xc0000000ULL;
  codec_scan_arith_model model;
  uint64_t low = 0u, high = top;
  size_t pending = 0u;
  size_t i;
  size_t capacity;

  if (count > (SIZE_MAX - 128u) / 16u) return DIC_STATUS_INVALID_ARGUMENT;
  capacity = count * 16u + 128u;
  if (codec_scan_bit_writer_ensure(capacity, writer) != DIC_STATUS_OK)
    return DIC_STATUS_MEMORY_ERROR;
  codec_scan_arith_model_init(&model);

  for (i = 0u; i < count; ++i) {
    unsigned int context = contexts[i];
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
        ++pending;
        low -= first_qtr;
        high -= first_qtr;
      } else {
        break;
      }
      if (out >= 0) {
        codec_scan_bit_write(out, writer);
        while (pending > 0u) {
          codec_scan_bit_write(!out, writer);
          --pending;
        }
      }
      low <<= 1u;
      high = (high << 1u) | 1u;
    }
  }
  ++pending;
  {
    int out = low < first_qtr ? 0 : 1;
    codec_scan_bit_write(out, writer);
    while (pending > 0u) {
      codec_scan_bit_write(!out, writer);
      --pending;
    }
  }
  codec_scan_bit_writer_flush(writer);
  return DIC_STATUS_OK;
}

/**
 * @brief Decodes the exact arithmetic refinement payload.
 */
static dic_status codec_scan_arithmetic_decode(
    const unsigned char* bytes, size_t bit_count,
    const unsigned char* contexts, size_t count, unsigned char* bits) {
  const uint64_t top = 0xffffffffULL;
  const uint64_t half = 0x80000000ULL;
  const uint64_t first_qtr = 0x40000000ULL;
  const uint64_t third_qtr = 0xc0000000ULL;
  codec_scan_arith_model model;
  codec_scan_bit_reader reader;
  uint64_t low = 0u, high = top, value = 0u;
  size_t i;

  if (count > 0u && (bytes == NULL || bits == NULL || bit_count == 0u))
    return DIC_HW4_FORMAT_ERROR;
  codec_scan_arith_model_init(&model);
  codec_scan_bit_reader_init(&reader, bytes, bit_count);
  for (i = 0u; i < 32u; ++i)
    value = (value << 1u) |
            (reader.bits_read < reader.bit_count
                 ? (uint64_t)codec_scan_bit_read(&reader)
                 : 0u);

  for (i = 0u; i < count; ++i) {
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
        value |= (uint64_t)codec_scan_bit_read(&reader);
    }
  }
  return DIC_STATUS_OK;
}

/**
 * @brief Builds the subordinate payload for coefficients significant earlier.
 *
 * Arithmetic mode is retained only when it is strictly smaller than the raw
 * one-bit-per-symbol representation.
 */
static dic_status codec_scan_encode_refinement_pass(
    const int32_t* plane, int width, int height, int bp,
    const codec_scan_sig_order* sig_order, size_t refinement_count,
    const unsigned char* significant, unsigned char* refinement_age,
    codec_scan_bitplane* bitplane) {
  unsigned char* bits = NULL;
  unsigned char* contexts = NULL;
  codec_scan_bit_writer arithmetic;
  size_t i;
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
  for (i = 0u; i < refinement_count; ++i) {
    size_t idx = sig_order->indices[i];
    uint32_t mag = plane[idx] < 0 ? (uint32_t)(-(plane[idx] + 1)) + 1u
                                  : (uint32_t)plane[idx];
    bits[i] = (unsigned char)((mag >> bp) & 1u);
    contexts[i] =
        refinement_age[idx] > 0u
            ? 2u
            : (unsigned char)(codec_scan_has_significant_neighbor(
                                  significant, width, height, idx)
                                  ? 1u
                                  : 0u);
    if (refinement_age[idx] < 255u) ++refinement_age[idx];
  }

  codec_scan_bit_writer_init(&arithmetic);
  status = codec_scan_arithmetic_encode(bits, contexts, refinement_count,
                                        &arithmetic);
  if (status == DIC_STATUS_OK && arithmetic.bit_count < refinement_count) {
    bitplane->subordinate_bits = arithmetic.bytes;
    bitplane->subordinate_bit_count = arithmetic.bit_count;
    bitplane->subordinate_byte_count = (arithmetic.bit_count + 7u) / 8u;
    bitplane->subordinate_mode = DIC_SCAN_REFINEMENT_ARITHMETIC;
  } else {
    codec_scan_bit_writer raw;
    free(arithmetic.bytes);
    codec_scan_bit_writer_init(&raw);
    status = codec_scan_bit_writer_ensure(refinement_count, &raw);
    if (status == DIC_STATUS_OK) {
      for (i = 0u; i < refinement_count; ++i)
        codec_scan_bit_write(bits[i], &raw);
      codec_scan_bit_writer_flush(&raw);
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

/* -------------------------------------------------------------------------- */
/*  Refinement pass decoder (one bitplane)                                    */
/* -------------------------------------------------------------------------- */

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
    contexts[i] =
        refinement_age[idx] > 0u
            ? 2u
            : (unsigned char)(codec_scan_has_significant_neighbor(
                                  significant, width, height, idx)
                                  ? 1u
                                  : 0u);
  }
  if (bitplane->subordinate_mode == DIC_SCAN_REFINEMENT_RAW) {
    codec_scan_bit_reader reader;
    if (bitplane->subordinate_bit_count != refinement_count) {
      status = DIC_HW4_FORMAT_ERROR;
      goto cleanup;
    }
    codec_scan_bit_reader_init(&reader, bitplane->subordinate_bits,
                               bitplane->subordinate_bit_count);
    for (i = 0u; i < refinement_count; ++i)
      bits[i] = (unsigned char)codec_scan_bit_read(&reader);
  } else if (bitplane->subordinate_mode ==
             DIC_SCAN_REFINEMENT_ARITHMETIC) {
    codec_scan_bit_writer canonical;
    status = codec_scan_arithmetic_decode(
        bitplane->subordinate_bits, bitplane->subordinate_bit_count, contexts,
        refinement_count, bits);
    if (status != DIC_STATUS_OK) goto cleanup;
    codec_scan_bit_writer_init(&canonical);
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

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

void codec_scan_bitplane_init(codec_scan_bitplane* bp) {
  if (bp == NULL) return;
  bp->dominant_token_count = 0u;
  bp->dominant_command_count = 0u;
  dic_hw2_huffman_bitstream_init(&bp->dominant_stream);
  bp->run_length_bits = NULL;
  bp->run_length_bit_count = 0u;
  bp->run_length_byte_count = 0u;
  bp->subordinate_bits = NULL;
  bp->subordinate_symbol_count = 0u;
  bp->subordinate_bit_count = 0u;
  bp->subordinate_byte_count = 0u;
  bp->subordinate_mode = DIC_SCAN_REFINEMENT_RAW;
}

void codec_scan_bitplane_free(codec_scan_bitplane* bp) {
  if (bp == NULL) return;
  dic_hw2_huffman_bitstream_free(&bp->dominant_stream);
  free(bp->run_length_bits);
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
  size_t plane_count;
  int max_bp, total_bp;
  codec_scan_bitplane* bps = NULL;
  unsigned char* significant = NULL;
  unsigned char* refinement_age = NULL;
  codec_scan_sig_order sig_order;
  dic_status status = DIC_STATUS_OK;
  int32_t* work_plane = NULL;
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
    /* An all-zero plane requires no bit-plane payload. */
    return DIC_STATUS_OK;
  }

  total_bp = max_bp + 1;
  bps = (codec_scan_bitplane*)calloc((size_t)total_bp, sizeof(bps[0]));
  if (bps == NULL) return DIC_STATUS_MEMORY_ERROR;

  /* Work on a copy (EZW modifies magnitudes in-place) */
  work_plane = (int32_t*)malloc(plane_count * sizeof(work_plane[0]));
  if (work_plane == NULL) {
    free(bps);
    return DIC_STATUS_MEMORY_ERROR;
  }
  memcpy(work_plane, plane, plane_count * sizeof(plane[0]));

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

  codec_scan_sig_order_init(&sig_order);

  /*
   * One output element is produced per threshold:
   *
   *   bp=max_bp  threshold=2^max_bp  -> output[0]
   *   bp=max_bp-1                    -> output[1]
   *   ...
   *   bp=0                           -> output[max_bp]
   */
  for (bp = max_bp; bp >= 0; --bp) {
    int32_t threshold = (int32_t)(1u << (unsigned)bp);
    codec_scan_token_buffer token_buf;
    codec_scan_token_buffer command_buf;
    codec_scan_bit_writer run_writer;
    unsigned char* visited = NULL;
    size_t sig_before = sig_order.count;
    codec_scan_bitplane* cur = bps + (max_bp - bp); /* MSB at index 0 */

    codec_scan_token_buffer_init(&token_buf);
    codec_scan_token_buffer_init(&command_buf);
    codec_scan_bit_writer_init(&run_writer);

    visited = (unsigned char*)calloc(plane_count, 1u);
    if (visited == NULL) {
      codec_scan_token_buffer_free(&token_buf);
      codec_scan_token_buffer_free(&command_buf);
      status = DIC_STATUS_MEMORY_ERROR;
      break;
    }

    /* Significance pass */
    status = codec_scan_encode_significance_pass(
        work_plane, width, height, levels, threshold, significant, visited,
        &token_buf, &sig_order);
    free(visited);
    if (status != DIC_STATUS_OK) {
      codec_scan_token_buffer_free(&token_buf);
      codec_scan_token_buffer_free(&command_buf);
      break;
    }

    status = codec_scan_rle_commands(&token_buf, &command_buf, &run_writer);
    if (status == DIC_STATUS_OK)
      status = codec_scan_encode_refinement_pass(
          work_plane, width, height, bp, &sig_order, sig_before, significant,
          refinement_age, cur);

    if (status != DIC_STATUS_OK) {
      free(run_writer.bytes);
      codec_scan_token_buffer_free(&token_buf);
      codec_scan_token_buffer_free(&command_buf);
      break;
    }

    cur->dominant_token_count = token_buf.count;
    cur->dominant_command_count = command_buf.count;
    cur->run_length_bits = run_writer.bytes;
    cur->run_length_bit_count = run_writer.bit_count;
    cur->run_length_byte_count = (run_writer.bit_count + 7u) / 8u;

    /* Huffman-encode commands with fixed table. */
    status = codec_scan_huffman_encode(command_buf.tokens, command_buf.count,
                                       &cur->dominant_stream);

    codec_scan_token_buffer_free(&token_buf);
    codec_scan_token_buffer_free(&command_buf);
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
     * We compute threshold from bp_idx and global max_bp: since we no longer
     * store max_bp explicitly in the bitplane struct, we compute it from
     * the total count. */
    int bp = total_bitplane_count - 1 - bp_idx;
    int32_t threshold = (int32_t)(1u << (unsigned)bp);
    unsigned char* tokens = NULL;
    unsigned char* commands = NULL;
    unsigned char* visited = NULL;
    size_t token_offset = 0u;
    size_t sig_before = sig_order.count;

    commands = cur->dominant_command_count == 0u
                   ? NULL
                   : (unsigned char*)malloc(cur->dominant_command_count);
    if (commands == NULL && cur->dominant_command_count > 0u) {
      status = DIC_STATUS_MEMORY_ERROR;
      goto cleanup;
    }
    status = codec_scan_huffman_decode(&cur->dominant_stream, commands,
                                       cur->dominant_command_count);
    if (status != DIC_STATUS_OK) {
      free(commands);
      goto cleanup;
    }
    status = codec_scan_expand_commands(
        commands, cur->dominant_command_count, cur->run_length_bits,
        cur->run_length_bit_count, cur->dominant_token_count, &tokens);
    free(commands);
    if (status != DIC_STATUS_OK) goto cleanup;

    visited = (unsigned char*)calloc(plane_count, 1u);
    if (visited == NULL) {
      free(tokens);
      status = DIC_STATUS_MEMORY_ERROR;
      goto cleanup;
    }

    /* Significance pass decode */
    {
      dic_rect_i32 ll_rect;
      int level;

      status = codec_subband_lowest_ll_rect(width, height, levels, &ll_rect);
      if (status != DIC_STATUS_OK) {
        free(tokens);
        free(visited);
        goto cleanup;
      }

      /* LL subband */
      {
        int y;
        for (y = 0; y < ll_rect.height && status == DIC_STATUS_OK; ++y) {
          int x;
          for (x = 0; x < ll_rect.width; ++x) {
            size_t idx = codec_scan_index(width, ll_rect.x + x, ll_rect.y + y);
            if (significant[idx] || visited[idx]) continue;
            if (token_offset >= cur->dominant_token_count) {
              status = DIC_HW4_FORMAT_ERROR;
              break;
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
              DIC_SUBBAND_HL, DIC_SUBBAND_LH, DIC_SUBBAND_HH};
          for (band = 0; band < 3 && status == DIC_STATUS_OK; ++band) {
            codec_subband_orientation orient = orientations[band];
            dic_rect_i32 rect;
            int y;
            status =
                codec_subband_rect(width, height, levels, level, orient, &rect);
            if (status != DIC_STATUS_OK) break;
            for (y = 0; y < rect.height && status == DIC_STATUS_OK; ++y) {
              int x;
              for (x = 0; x < rect.width; ++x) {
                int image_x = rect.x + x;
                int image_y = rect.y + y;
                size_t idx = codec_scan_index(width, image_x, image_y);
                if (significant[idx] || visited[idx]) continue;
                if (token_offset >= cur->dominant_token_count) {
                  status = DIC_HW4_FORMAT_ERROR;
                  break;
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
                    codec_scan_mark_descendants_visited(visited, width, height,
                                                        level, orient, x, y);
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
  free(refinement_age);
  free(significant);
  return status;
}

#pragma once
/**
 * @file scan.h
 * @brief Full-plane zerotree significance and context refinement coding.
 *
 * Each coefficient plane is encoded from its most significant nonzero bit to
 * bit zero. One @ref codec_scan_bitplane contains three logical payloads:
 *
 * @code{.unparsed}
 * coefficient bit-plane
 *        |
 *        +--> dominant significance tokens
 *        |      token stream -> IZ run folding -> fixed Huffman bytes
 *        |                         |
 *        |                         `-> Exp-Golomb run side stream
 *        |
 *        `--> subordinate refinement bits
 *               -> adaptive arithmetic bytes when smaller
 *               -> raw LSB-first bytes otherwise
 *
 * codec_scan_bitplane
 * +------------------------+
 * | dominant_stream        | Huffman-coded commands
 * | run_length_bits        | Exp-Golomb payload for ZRUN commands
 * | subordinate_bits       | raw or arithmetic refinement payload
 * +------------------------+
 * @endcode
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"
#include "hw2/hw2_huffman.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Symbols emitted by the dominant significance pass. */
typedef enum codec_scan_token {
  /** Insignificant coefficient whose descendants are not all insignificant.
   */
  DIC_SCAN_TOKEN_IZ = 0,
  /** Insignificant coefficient representing an insignificant descendant tree.
   */
  DIC_SCAN_TOKEN_ZTR = 1,
  /** Newly significant positive coefficient. */
  DIC_SCAN_TOKEN_POS = 2,
  /** Newly significant negative coefficient. */
  DIC_SCAN_TOKEN_NEG = 3,
  /** Command replacing a profitable run of five or more IZ tokens. */
  DIC_SCAN_TOKEN_ZRUN = 4,
  /** Number of token symbols in the fixed Huffman alphabet. */
  DIC_SCAN_TOKEN_COUNT = 5
} codec_scan_token;

/** Default probabilities used to construct the fixed Huffman table. */
extern const double codec_scan_fixed_probs[DIC_SCAN_TOKEN_COUNT];

/** Storage mode selected for subordinate refinement symbols. */
typedef enum codec_scan_refinement_mode {
  /** One LSB-first stored bit per refinement symbol. */
  DIC_SCAN_REFINEMENT_RAW = 0,
  /** Adaptive three-context binary arithmetic coding. */
  DIC_SCAN_REFINEMENT_ARITHMETIC = 1
} codec_scan_refinement_mode;

/**
 * @brief Encoded representation of one MSB-first quality layer.
 *
 * All pointer members are owned by the object and released by
 * codec_scan_bitplane_free().
 */
typedef struct codec_scan_bitplane {
  /** Expanded dominant token count before IZ run folding. */
  size_t dominant_token_count;
  /** Huffman command count after IZ run folding. */
  size_t dominant_command_count;
  /** Per-symbol counts for the commands sent to the Huffman encoder. */
  size_t dominant_symbol_counts[DIC_SCAN_TOKEN_COUNT];
  /** Fixed-table Huffman payload for dominant commands. */
  dic_hw2_huffman_bitstream dominant_stream;
  /** Exp-Golomb side payload used by ZRUN commands. */
  unsigned char* run_length_bits;
  /** Number of meaningful bits in @ref run_length_bits. */
  size_t run_length_bit_count;
  /** Allocated/stored bytes in @ref run_length_bits. */
  size_t run_length_byte_count;
  /** Raw or arithmetic-coded subordinate refinement payload. */
  unsigned char* subordinate_bits;
  /** Number of coefficients refined by this layer. */
  size_t subordinate_symbol_count;
  /** Number of meaningful bits in @ref subordinate_bits. */
  size_t subordinate_bit_count;
  /** Allocated/stored bytes in @ref subordinate_bits. */
  size_t subordinate_byte_count;
  /** Interpretation of @ref subordinate_bits. */
  codec_scan_refinement_mode subordinate_mode;
} codec_scan_bitplane;

/** @brief Initializes an empty bit-plane object. */
void codec_scan_bitplane_init(codec_scan_bitplane* bitplane);
/** @brief Releases all bit-plane payloads and resets the object. */
void codec_scan_bitplane_free(codec_scan_bitplane* bitplane);

/**
 * @brief Encodes a packed DWT coefficient plane into MSB-first layers.
 * @param plane Signed coefficients in row-major packed-subband order.
 * @param width Plane width.
 * @param height Plane height.
 * @param levels Number of DWT levels represented by the packed plane.
 * @param bitplanes_out Receives a calloc-owned array; release each element
 * with codec_scan_bitplane_free(), then free the array.
 * @param bitplane_count_out Receives the number of layers. An all-zero plane
 * produces zero layers and a NULL array.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_scan_encode_plane(const int32_t* plane, int width, int height,
                                   int levels,
                                   codec_scan_bitplane** bitplanes_out,
                                   int* bitplane_count_out);

/**
 * @brief Decodes an MSB-first prefix into a packed coefficient plane.
 * @param bitplanes Complete serialized layer array.
 * @param total_bitplane_count Total layer count used to derive bit weights.
 * @param num_bitplanes Prefix length to decode; must be positive.
 * @param width Output plane width.
 * @param height Output plane height.
 * @param levels Packed DWT level count.
 * @param plane Caller-owned output array of `width * height` coefficients.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_scan_decode_plane(const codec_scan_bitplane* bitplanes,
                                   int total_bitplane_count, int num_bitplanes,
                                   int width, int height, int levels,
                                   int32_t* plane);

/** @brief Returns the magnitude bit width of a signed coefficient. */
unsigned char codec_scan_amplitude_size(int32_t amplitude);
/** @brief Exports the active fixed canonical Huffman code lengths. */
void codec_scan_get_code_lengths(unsigned char lengths[DIC_SCAN_TOKEN_COUNT]);
/**
 * @brief Rebuilds the fixed canonical Huffman table from serialized lengths.
 * @param lengths One code length per @ref codec_scan_token.
 */
void codec_scan_set_code_lengths(
    const unsigned char lengths[DIC_SCAN_TOKEN_COUNT]);

#ifdef __cplusplus
}
#endif

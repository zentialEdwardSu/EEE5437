#pragma once

/**
 * @file j2k_packet.h
 * @brief JPEG 2000 packet header construction and parsing API.
 *
 * Implements T.800 Annex B.10 — packet header construction. A JPEG 2000
 * packet is the fundamental unit of compressed data organisation. Each
 * packet (for one layer, resolution level, component, and precinct)
 * consists of:
 *
 *   - A packet header containing per-code-block metadata:
 *       - Inclusion tag-tree bits (B.10.4): signals whether a code-block
 *         first contributes to this layer
 *       - Zero bit-plane tag-tree bits (B.10.5): number of leading
 *         insignificant magnitude bit-planes at first inclusion
 *       - Coding-pass count (B.10.5): number of contribution passes
 *       - Lblock and pass-length words (B.10.5): byte counts per segment
 *   - A packet body containing the concatenated MQ-coded code-block
 *     contribution bytes in raster code-block order.
 *
 * Packet headers use bit-stream syntax (Annex B.10.3): an empty-packet
 * bit signals whether any code-block contributions exist, followed by
 * sub-band code-block metadata encoded using tag trees and unary/sign-mag
 * length representations. An SOP marker (0xFF91) optionally precedes
 * the packet, and an EPH marker (0xFF92) optionally follows the header
 * byte alignment.
 *
 * This module provides:
 *   - Bit-level packet header writing (j2k_packet_header)
 *   - Encoder-side assembly from EBCOT streams
 *   - Decoder-side parsing with stateful tag-tree tracking
 *   - Layer-aware truncation selection with rate-distortion slopes
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex B.10 (packet headers)
 * - paper/T-REC-T.800-200208.pdf, Annex B.10.2-B.10.5 (code-block contribution syntax)
 * - paper/T-REC-T.800-200208.pdf, Annex B.10.6 (SOP marker)
 * - paper/T-REC-T.800-200208.pdf, Annex B.10.7 (EPH marker)
 * - paper/T-REC-T.800-200208.pdf, Annex B.10.8 (LRCP progression)
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"
#include "j2k/j2k_ebcot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Byte-level packet-header builder.
 *
 * Accumulates bits into a byte buffer, handling 0xFF bit-stuffing
 * (seven data bits per byte after an 0xFF) and final byte alignment
 * (Annex B.10.3).
 */
typedef struct j2k_packet_header
{
    /** Packed packet-header bytes. */
    uint8_t *data;
    /** Number of valid bytes in data. */
    size_t size;
    /** Allocated byte capacity of data. */
    size_t capacity;
    /** Partially assembled packet-header byte. */
    uint8_t current_byte;
    /** Bits already written into current_byte. */
    unsigned int bits_used;
    /** Bits remaining before byte emission; seven after 0xFF stuffing. */
    unsigned int bits_available;
    /** Non-zero after the packet header has been byte-aligned (j2k_packet_header_finish). */
    int finished;
} j2k_packet_header;

/**
 * @brief Per-code-block metadata for one packet contribution.
 *
 * Contains all information encoded in the packet header for a single
 * code-block (Annex B.10.4-B.10.5).
 */
typedef struct j2k_packet_codeblock
{
    /** Non-zero when this packet includes a code-block contribution. */
    int included;
    /** Non-zero when this is the first included contribution for the code-block. */
    int first_inclusion;
    /** Zero bit-plane count encoded with tag-trees at first inclusion. */
    uint32_t zero_bitplanes;
    /** Number of coding passes contributed by this packet. */
    uint32_t coding_passes;
    /** Lblock value before this packet contribution's increment bits. */
    uint32_t lblock;
    /** Increment to the packet-header Lblock value. */
    uint32_t lblock_increment;
    /** Length in bytes of the contributed codeword segment. */
    uint32_t codeword_length;
    /** Optional per-terminated-pass segment lengths (NULL = aggregate). */
    const size_t *segment_lengths;
    /** Number of entries in segment_lengths; 0 means one aggregate segment. */
    uint32_t segment_count;
} j2k_packet_codeblock;

/**
 * @brief One code-block's packet contribution (header metadata + body bytes).
 */
typedef struct j2k_packet_codeblock_payload
{
    /** Packet-header metadata for this code-block. */
    j2k_packet_codeblock header;
    /** Code-block contribution bytes appended after the packet header. */
    const uint8_t *codeword;
    /** Number of bytes in codeword. */
    size_t codeword_size;
} j2k_packet_codeblock_payload;

/**
 * @brief Sub-band description for tagged packet construction.
 *
 * Used by the encoder to describe which code-block streams belong to a
 * particular sub-band and, optionally, to constrain the packet to a
 * precinct window within the sub-band.
 */
typedef struct j2k_packet_subband_payload
{
    /** Raster-ordered code-block streams for the complete sub-band. */
    const j2k_codeblock_stream *streams;
    /** Number of complete sub-band entries in streams. */
    size_t stream_count;
    /** Complete sub-band code-block count in the horizontal direction. */
    int blocks_x;
    /** Complete sub-band code-block count in the vertical direction. */
    int blocks_y;
    /** First code-block column included by this packet precinct; 0 for full sub-band. */
    int first_block_x;
    /** First code-block row included by this packet precinct; 0 for full sub-band. */
    int first_block_y;
    /** Code-block columns in the precinct window; 0 selects all. */
    int packet_blocks_x;
    /** Code-block rows in the precinct window; 0 selects all. */
    int packet_blocks_y;
} j2k_packet_subband_payload;

/**
 * @brief Sub-band geometry for packet-header parsing.
 */
typedef struct j2k_packet_subband_layout
{
    /** Complete sub-band code-block count in the horizontal direction. */
    int blocks_x;
    /** Complete sub-band code-block count in the vertical direction. */
    int blocks_y;
    /** First code-block column included by this packet precinct. */
    int first_block_x;
    /** First code-block row included by this packet precinct. */
    int first_block_y;
    /** Code-block columns in the precinct window; 0 selects all. */
    int packet_blocks_x;
    /** Code-block rows in the precinct window; 0 selects all. */
    int packet_blocks_y;
} j2k_packet_subband_layout;

/**
 * @brief One coding-pass byte range from a parsed packet.
 */
typedef struct j2k_packet_pass_range
{
    /** Sub-band index within the parsed packet. */
    size_t subband_index;
    /** Raster-order code-block index within the sub-band. */
    size_t codeblock_index;
    /** Absolute coding-pass index before this packet contribution. */
    uint32_t pass_index;
    /** Byte offset from the start of the packet payload. */
    size_t byte_offset;
    /** Number of bytes in this coding-pass contribution. */
    size_t byte_count;
} j2k_packet_pass_range;

/**
 * @brief Parsed packet result from j2k_packet_header_parser_parse().
 */
typedef struct j2k_packet_parse_result
{
    /** Parsed code-block pass byte ranges (caller-owned after parse). */
    j2k_packet_pass_range *ranges;
    /** Number of valid entries in ranges. */
    size_t range_count;
    /** Allocated entry capacity of ranges. */
    size_t range_capacity;
    /** Number of bytes occupied by the packet header. */
    size_t packet_header_size;
    /** Number of bytes occupied by code-block contribution data. */
    size_t packet_body_size;
    /** Non-zero when the packet empty bit is zero (packet is empty). */
    int is_empty;
} j2k_packet_parse_result;

/** @brief Opaque stateful packet-header parser. */
typedef struct j2k_packet_header_parser j2k_packet_header_parser;

/** @brief Initialise a packet header buffer to an empty state. */
void j2k_packet_header_init(j2k_packet_header *header);
/** @brief Free the byte buffer owned by a packet header. */
void j2k_packet_header_free(j2k_packet_header *header);
/** @brief Initialise a packet parse result. */
void j2k_packet_parse_result_init(j2k_packet_parse_result *result);
/** @brief Free all memory owned by a packet parse result. */
void j2k_packet_parse_result_free(j2k_packet_parse_result *result);

/**
 * @brief Create a stateful packet-header parser.
 *
 * Allocates and initialises a parser that tracks tag-tree state across
 * successive packets. The parser maintains inclusion and zero-bit-plane
 * tag-tree state machines per sub-band code-block, allowing incremental
 * decoding of multiple quality layers.
 *
 * @param subbands Array of sub-band layouts describing the code-block grid.
 * @param subband_count Number of entries in @p subbands.
 * @param terminated_passes Non-zero if code-block style marks each pass as
 *                         terminated (allowing per-pass byte ranges).
 * @param parser [out] Receives the allocated parser. Caller must destroy
 *               with j2k_packet_header_parser_destroy().
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_MEMORY_ERROR if allocation fails.
 */
dic_status j2k_packet_header_parser_create(
    const j2k_packet_subband_layout *subbands,
    size_t subband_count,
    int terminated_passes,
    j2k_packet_header_parser **parser
);

/** @brief Destroy a packet-header parser and its tag-tree state. */
void j2k_packet_header_parser_destroy(j2k_packet_header_parser *parser);

/**
 * @brief Parse one packet payload into per-pass byte ranges.
 *
 * Reads the packet header bitstream, decodes tag-tree inclusion and
 * zero-bit-plane values, reads coding-pass counts and segment lengths,
 * and records per-pass byte ranges. Excludes SOP/EPH markers (the
 * caller strips those before calling).
 *
 * @param parser Stateful parser with accumulated tag-tree state.
 * @param payload Packet payload bytes (no SOP/EPH markers).
 * @param payload_size Number of bytes in @p payload.
 * @param layer_index Current quality layer index (for tag-tree decoding).
 * @param result [out] Receives parsed pass ranges and sizes.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_header_parser_parse(
    j2k_packet_header_parser *parser,
    const uint8_t *payload,
    size_t payload_size,
    uint16_t layer_index,
    j2k_packet_parse_result *result
);

/**
 * @brief Append one bit to a packet header under construction.
 *
 * @param header Packet header to append to.
 * @param bit Bit value (0 or 1).
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_header_append_bit(
    j2k_packet_header *header,
    unsigned int bit
);

/**
 * @brief Byte-align and finalise a packet header.
 *
 * Flushes the current partial byte, emitting stuffing bits as needed,
 * and marks the header as finished.
 *
 * @param header Packet header to finalise.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_header_finish(j2k_packet_header *header);

/**
 * @brief Build an empty packet header (empty-packet bit = 0, aligned).
 *
 * @param header [out] Receives the empty packet header bytes.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_build_empty_header(j2k_packet_header *header);

/**
 * @brief Append a unary-coded zero-run followed by a one bit.
 *
 * Encodes @p zero_count zeros followed by a single one bit.
 * Used for tag-tree threshold encoding.
 *
 * @param header Packet header to write to.
 * @param zero_count Number of zero bits to emit before the one bit.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_header_append_unary_zeros_then_one(
    j2k_packet_header *header,
    uint32_t zero_count
);

/**
 * @brief Append one code-block contribution to the packet header.
 *
 * Writes inclusion, zero-bit-plane (if first inclusion), coding-pass
 * count, Lblock increment, and pass-length fields per Annex B.10.4-B.10.5.
 *
 * @param header Packet header to append to.
 * @param codeblock Code-block metadata for this contribution.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_header_append_codeblock(
    j2k_packet_header *header,
    const j2k_packet_codeblock *codeblock
);

/**
 * @brief Calculate Lblock increment for a packet contribution.
 *
 * The Lblock value sets the bit-width for coding-pass length fields.
 * This function computes the increment needed to accommodate the
 * codeword's segment lengths.
 *
 * @param current_lblock Current Lblock value before this contribution.
 * @param coding_passes Number of coding passes in this contribution.
 * @param codeword_length Total codeword length in bytes.
 * @param increment [out] Receives the Lblock increment (per B.10.5).
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_calculate_lblock_increment(
    uint32_t current_lblock,
    uint32_t coding_passes,
    uint32_t codeword_length,
    uint32_t *increment
);

/**
 * @brief Prepare code-block metadata for packet inclusion.
 *
 * Fills a j2k_packet_codeblock from the raw zero-bit-plane count,
 * coding-pass count, and codeword length. Automatically computes
 * Lblock increment and determines inclusion/first-inclusion flags.
 *
 * @param codeblock [out] Receives prepared metadata.
 * @param zero_bitplanes Zero bit-plane count for this code-block.
 * @param coding_passes Number of contribution passes.
 * @param codeword_length Total codeword length in bytes.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_prepare_codeblock(
    j2k_packet_codeblock *codeblock,
    uint32_t zero_bitplanes,
    uint32_t coding_passes,
    uint32_t codeword_length
);

/**
 * @brief Build a single-codeblock packet payload.
 *
 * @param codeblock Metadata for the one code-block.
 * @param codeword Contribution bytes.
 * @param codeword_size Number of bytes in @p codeword.
 * @param payload [out] Receives the packet header and concatenated body.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_build_single_codeblock_payload(
    const j2k_packet_codeblock *codeblock,
    const uint8_t *codeword,
    size_t codeword_size,
    j2k_packet_header *payload
);

/**
 * @brief Build a packet payload from individual code-block contributions.
 *
 * @param codeblocks Array of code-block contributions with metadata.
 * @param codeblock_count Number of contributions.
 * @param payload [out] Receives the assembled packet.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_build_codeblock_payload(
    const j2k_packet_codeblock_payload *codeblocks,
    size_t codeblock_count,
    j2k_packet_header *payload
);

/**
 * @brief Build one packet from EBCOT code-block streams.
 *
 * Extracts all coding passes from every stream into a single-layer packet.
 *
 * @param streams EBCOT code-block streams.
 * @param stream_count Number of streams.
 * @param payload [out] Receives the packet.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_build_ebcot_payload(
    const j2k_codeblock_stream *streams,
    size_t stream_count,
    j2k_packet_header *payload
);

/**
 * @brief Build a packet from sub-band-tagged EBCOT streams.
 *
 * Similar to j2k_packet_build_ebcot_payload() but organises code-blocks
 * by sub-band with optional precinct windows. Includes the empty-packet
 * bit and final alignment.
 *
 * @param subbands Sub-band descriptors with code-block streams.
 * @param subband_count Number of sub-bands.
 * @param payload [out] Receives the packet.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_build_tagged_ebcot_payload(
    const j2k_packet_subband_payload *subbands,
    size_t subband_count,
    j2k_packet_header *payload
);

/**
 * @brief Build a tagged packet and return the packet-header size.
 *
 * Same as j2k_packet_build_tagged_ebcot_payload() but also returns
 * the byte count of the packet header (before the code-block
 * contribution bytes begin). This is used by the encoder to separate
 * header writing from body assembly.
 *
 * @param subbands Sub-band descriptors.
 * @param subband_count Number of sub-bands.
 * @param payload [out] Receives the complete packet.
 * @param packet_header_size [out] Byte count of the packet header portion.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_build_tagged_ebcot_payload_with_header_size(
    const j2k_packet_subband_payload *subbands,
    size_t subband_count,
    j2k_packet_header *payload,
    size_t *packet_header_size
);

/**
 * @brief Build one LRCP quality-layer packet from terminated-pass streams.
 *
 * Selects coding-pass contributions for one quality layer from all
 * code-block streams. Uses the per-pass rate-distortion slopes stored
 * in each stream to determine which passes belong to this layer.
 * The @p layer_index selects passes up to a cumulative rate target,
 * and @p layers informs the layer-relative slope thresholds.
 *
 * @param subbands Sub-band descriptors with code-block streams.
 * @param subband_count Number of sub-bands.
 * @param layer_index Zero-based layer index to build (0 to layers-1).
 * @param layers Total number of quality layers.
 * @param payload [out] Receives the assembled packet.
 * @param packet_header_size [out] Byte count of the packet header.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_build_tagged_ebcot_layer_payload_with_header_size(
    const j2k_packet_subband_payload *subbands,
    size_t subband_count,
    uint16_t layer_index,
    uint16_t layers,
    j2k_packet_header *payload,
    size_t *packet_header_size
);

/**
 * @brief Build empty LRCP packets for all positions.
 *
 * Emits one empty-packet bit per packet position in LRCP order.
 * All packets have zero code-block contributions. Used for testing
 * the packet-count logic.
 *
 * @param components Number of image components.
 * @param decomposition_levels Number of DWT levels.
 * @param layers Number of quality layers.
 * @param payload [out] Receives the concatenated empty packets.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_packet_build_empty_lrcp_payload(
    uint16_t components,
    uint8_t decomposition_levels,
    uint16_t layers,
    j2k_packet_header *payload
);

#ifdef __cplusplus
}
#endif

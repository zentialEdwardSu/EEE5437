#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"
#include "j2k/dic_j2k_ebcot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_j2k_packet_header
{
    uint8_t *data; /**< Packed packet-header bytes. */
    size_t size; /**< Number of valid bytes in data. */
    size_t capacity; /**< Allocated byte capacity of data. */
    uint8_t current_byte; /**< Partially assembled packet-header byte. */
    unsigned int bits_used; /**< Bits already written into current_byte. */
    unsigned int bits_available; /**< Bits remaining before byte emission; seven after 0xFF stuffing. */
    int finished; /**< Non-zero after the packet header has been byte aligned. */
} dic_j2k_packet_header;

typedef struct dic_j2k_packet_codeblock
{
    int included; /**< Non-zero when this packet includes a code-block contribution. */
    int first_inclusion; /**< Non-zero when this is the first included contribution for the code-block. */
    uint32_t zero_bitplanes; /**< Zero bit-plane count encoded with tag-trees for first inclusion. */
    uint32_t coding_passes; /**< Number of coding passes contributed by this packet. */
    uint32_t lblock; /**< Lblock value before this packet contribution's increment bits are applied. */
    uint32_t lblock_increment; /**< Increment to the packet-header Lblock value. */
    uint32_t codeword_length; /**< Length in bytes of the contributed codeword segment. */
    const size_t *segment_lengths; /**< Optional per-terminated-pass segment lengths for this contribution. */
    uint32_t segment_count; /**< Number of entries in segment_lengths; zero means one aggregate segment. */
} dic_j2k_packet_codeblock;

typedef struct dic_j2k_packet_codeblock_payload
{
    dic_j2k_packet_codeblock header; /**< Packet-header metadata for this code-block. */
    const uint8_t *codeword; /**< Code-block contribution bytes appended after the packet header. */
    size_t codeword_size; /**< Number of bytes in codeword. */
} dic_j2k_packet_codeblock_payload;

typedef struct dic_j2k_packet_subband_payload
{
    const dic_j2k_codeblock_stream *streams; /**< Raster-ordered code-block streams for the complete sub-band. */
    size_t stream_count; /**< Number of complete sub-band entries in streams. */
    int blocks_x; /**< Complete sub-band code-block count in the horizontal direction. */
    int blocks_y; /**< Complete sub-band code-block count in the vertical direction. */
    int first_block_x; /**< First code-block column included by this packet precinct; zero for full sub-band packets. */
    int first_block_y; /**< First code-block row included by this packet precinct; zero for full sub-band packets. */
    int packet_blocks_x; /**< Code-block columns included by this packet precinct; zero selects the full sub-band width. */
    int packet_blocks_y; /**< Code-block rows included by this packet precinct; zero selects the full sub-band height. */
} dic_j2k_packet_subband_payload;

typedef struct dic_j2k_packet_subband_layout
{
    int blocks_x; /**< Complete sub-band code-block count in the horizontal direction. */
    int blocks_y; /**< Complete sub-band code-block count in the vertical direction. */
    int first_block_x; /**< First code-block column included by this packet precinct. */
    int first_block_y; /**< First code-block row included by this packet precinct. */
    int packet_blocks_x; /**< Code-block columns included by this packet precinct; zero selects the full sub-band width. */
    int packet_blocks_y; /**< Code-block rows included by this packet precinct; zero selects the full sub-band height. */
} dic_j2k_packet_subband_layout;

typedef struct dic_j2k_packet_pass_range
{
    size_t subband_index; /**< Sub-band index within the parsed packet. */
    size_t codeblock_index; /**< Raster-order code-block index within the sub-band. */
    uint32_t pass_index; /**< Absolute coding-pass index in the code-block before this packet contribution. */
    size_t byte_offset; /**< Byte offset of this pass contribution from the start of the packet payload. */
    size_t byte_count; /**< Number of bytes in this coding-pass contribution. */
} dic_j2k_packet_pass_range;

typedef struct dic_j2k_packet_parse_result
{
    dic_j2k_packet_pass_range *ranges; /**< Parsed code-block pass byte ranges owned by this result. */
    size_t range_count; /**< Number of valid entries in ranges. */
    size_t range_capacity; /**< Allocated entry capacity of ranges. */
    size_t packet_header_size; /**< Number of bytes occupied by the packet header. */
    size_t packet_body_size; /**< Number of bytes occupied by code-block contribution data. */
    int is_empty; /**< Non-zero when the packet empty bit is zero. */
} dic_j2k_packet_parse_result;

typedef struct dic_j2k_packet_header_parser dic_j2k_packet_header_parser;

void dic_j2k_packet_header_init(dic_j2k_packet_header *header);
void dic_j2k_packet_header_free(dic_j2k_packet_header *header);
void dic_j2k_packet_parse_result_init(dic_j2k_packet_parse_result *result);
void dic_j2k_packet_parse_result_free(dic_j2k_packet_parse_result *result);

dic_status dic_j2k_packet_header_parser_create(
    const dic_j2k_packet_subband_layout *subbands,
    size_t subband_count,
    int terminated_passes, /**< Non-zero when code-block style terminates each coding pass, allowing per-pass byte ranges. */
    dic_j2k_packet_header_parser **parser
);

void dic_j2k_packet_header_parser_destroy(dic_j2k_packet_header_parser *parser);

/** Parses one packet payload, excluding SOP/EPH markers, and reconstructs stateful code-block pass byte ranges. */
dic_status dic_j2k_packet_header_parser_parse(
    dic_j2k_packet_header_parser *parser,
    const uint8_t *payload,
    size_t payload_size,
    uint16_t layer_index,
    dic_j2k_packet_parse_result *result
);

dic_status dic_j2k_packet_header_append_bit(
    dic_j2k_packet_header *header,
    unsigned int bit
);

dic_status dic_j2k_packet_header_finish(dic_j2k_packet_header *header);

dic_status dic_j2k_packet_build_empty_header(dic_j2k_packet_header *header);

dic_status dic_j2k_packet_header_append_unary_zeros_then_one(
    dic_j2k_packet_header *header,
    uint32_t zero_count
);

dic_status dic_j2k_packet_header_append_codeblock(
    dic_j2k_packet_header *header,
    const dic_j2k_packet_codeblock *codeblock
);

dic_status dic_j2k_packet_calculate_lblock_increment(
    uint32_t current_lblock,
    uint32_t coding_passes,
    uint32_t codeword_length,
    uint32_t *increment
);

dic_status dic_j2k_packet_prepare_codeblock(
    dic_j2k_packet_codeblock *codeblock,
    uint32_t zero_bitplanes,
    uint32_t coding_passes,
    uint32_t codeword_length
);

dic_status dic_j2k_packet_build_single_codeblock_payload(
    const dic_j2k_packet_codeblock *codeblock,
    const uint8_t *codeword,
    size_t codeword_size,
    dic_j2k_packet_header *payload
);

dic_status dic_j2k_packet_build_codeblock_payload(
    const dic_j2k_packet_codeblock_payload *codeblocks,
    size_t codeblock_count,
    dic_j2k_packet_header *payload
);

dic_status dic_j2k_packet_build_ebcot_payload(
    const dic_j2k_codeblock_stream *streams,
    size_t stream_count,
    dic_j2k_packet_header *payload
);

dic_status dic_j2k_packet_build_tagged_ebcot_payload(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count,
    dic_j2k_packet_header *payload
);

/** Builds one packet and returns the byte offset where the packet header ends. */
dic_status dic_j2k_packet_build_tagged_ebcot_payload_with_header_size(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count,
    dic_j2k_packet_header *payload,
    size_t *packet_header_size
);

/** Builds one LRCP layer packet from terminated coding-pass truncation ranges. */
dic_status dic_j2k_packet_build_tagged_ebcot_layer_payload_with_header_size(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count,
    uint16_t layer_index,
    uint16_t layers,
    dic_j2k_packet_header *payload,
    size_t *packet_header_size
);

dic_status dic_j2k_packet_build_empty_lrcp_payload(
    uint16_t components,
    uint8_t decomposition_levels,
    uint16_t layers,
    dic_j2k_packet_header *payload
);

#ifdef __cplusplus
}
#endif

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
    uint32_t lblock_increment; /**< Increment to the packet-header Lblock value. */
    uint32_t codeword_length; /**< Length in bytes of the contributed codeword segment. */
} dic_j2k_packet_codeblock;

typedef struct dic_j2k_packet_codeblock_payload
{
    dic_j2k_packet_codeblock header; /**< Packet-header metadata for this code-block. */
    const uint8_t *codeword; /**< Code-block contribution bytes appended after the packet header. */
    size_t codeword_size; /**< Number of bytes in codeword. */
} dic_j2k_packet_codeblock_payload;

typedef struct dic_j2k_packet_subband_payload
{
    const dic_j2k_codeblock_stream *streams; /**< Raster-ordered code-block streams for a sub-band. */
    size_t stream_count; /**< Number of entries in streams. */
    int blocks_x; /**< Code-block count in the horizontal direction. */
    int blocks_y; /**< Code-block count in the vertical direction. */
} dic_j2k_packet_subband_payload;

void dic_j2k_packet_header_init(dic_j2k_packet_header *header);
void dic_j2k_packet_header_free(dic_j2k_packet_header *header);

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

dic_status dic_j2k_packet_build_empty_lrcp_payload(
    uint16_t components,
    uint8_t decomposition_levels,
    uint16_t layers,
    dic_j2k_packet_header *payload
);

#ifdef __cplusplus
}
#endif

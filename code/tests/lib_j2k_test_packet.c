#include "j2k/dic_j2k_packet.h"
#include "test_helpers.h"

#include <string.h>

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.1 and B.10.3, packet header bit stuffing and empty packet syntax. */
int main(void)
{
    dic_j2k_packet_header header;
    dic_j2k_packet_header payload;
    dic_j2k_packet_header nonempty_payload;
    dic_j2k_packet_codeblock codeblock;
    dic_j2k_packet_codeblock prepared;
    dic_j2k_packet_codeblock_payload contributions[2];
    dic_j2k_codeblock_stream tagged_streams[3];
    dic_j2k_codeblock_stream precinct_streams[4];
    dic_j2k_packet_subband_payload tagged_subband;
    dic_j2k_packet_subband_payload precinct_subband;
    dic_j2k_packet_subband_layout parser_layout;
    dic_j2k_packet_subband_layout precinct_parser_layout;
    dic_j2k_packet_header_parser *parser = NULL;
    dic_j2k_packet_parse_result parse_result;
    const uint8_t codeword[] = {0x12u, 0x34u, 0x56u};
    const uint8_t second_codeword[] = {0xabu, 0xcdu};
    uint8_t tagged_codewords[] = {0x11u, 0x22u, 0x33u};
    uint8_t precinct_codewords[] = {0x41u, 0x42u, 0x43u, 0x44u};
    const int32_t coefficients[] = {
        0, 7, -2, 0,
        5, 0, 0, -9,
        0, 1, -1, 0,
        4, 0, 0, 3
    };
    dic_j2k_codeblock_stream eb_stream;
    dic_j2k_packet_subband_payload eb_subband;
    size_t layer_header_size;
    size_t first_layer_bytes;
    size_t second_layer_bytes;
    size_t expected_offset;
    size_t parsed_bytes;
    unsigned int i;
    uint32_t increment;

    dic_j2k_packet_header_init(&header);
    dic_j2k_packet_header_init(&payload);
    dic_j2k_packet_parse_result_init(&parse_result);
    dic_j2k_codeblock_stream_init(&eb_stream);
    memset(&tagged_subband, 0, sizeof(tagged_subband));
    memset(&precinct_subband, 0, sizeof(precinct_subband));
    memset(&parser_layout, 0, sizeof(parser_layout));
    memset(&precinct_parser_layout, 0, sizeof(precinct_parser_layout));
    memset(&eb_subband, 0, sizeof(eb_subband));
    for (i = 0u; i < 3u; ++i)
        dic_j2k_codeblock_stream_init(tagged_streams + i);
    for (i = 0u; i < 4u; ++i)
        dic_j2k_codeblock_stream_init(precinct_streams + i);

    DIC_EXPECT(dic_j2k_packet_build_empty_header(&header) == DIC_STATUS_OK);
    DIC_EXPECT(header.size == 1u);
    DIC_EXPECT(header.data[0] == 0x00u);
    dic_j2k_packet_header_free(&header);

    dic_j2k_packet_header_init(&header);
    for (i = 0u; i < 9u; ++i)
        DIC_EXPECT(dic_j2k_packet_header_append_bit(&header, 1u) == DIC_STATUS_OK);
    DIC_EXPECT(dic_j2k_packet_header_finish(&header) == DIC_STATUS_OK);
    DIC_EXPECT(header.size == 2u);
    DIC_EXPECT(header.data[0] == 0xffu);
    DIC_EXPECT(header.data[1] == 0x40u);
    dic_j2k_packet_header_free(&header);

    dic_j2k_packet_header_init(&header);
    DIC_EXPECT(dic_j2k_packet_calculate_lblock_increment(3u, 1u, 7u, &increment) == DIC_STATUS_OK);
    DIC_EXPECT(increment == 0u);
    DIC_EXPECT(dic_j2k_packet_calculate_lblock_increment(3u, 1u, 8u, &increment) == DIC_STATUS_OK);
    DIC_EXPECT(increment == 1u);
    DIC_EXPECT(dic_j2k_packet_calculate_lblock_increment(3u, 9u, 31u, &increment) == DIC_STATUS_OK);
    DIC_EXPECT(increment == 0u);

    codeblock.included = 1;
    codeblock.first_inclusion = 1;
    codeblock.zero_bitplanes = 2u;
    codeblock.coding_passes = 1u;
    codeblock.lblock = 3u;
    codeblock.lblock_increment = 1u;
    codeblock.codeword_length = 3u;
    codeblock.segment_lengths = NULL;
    codeblock.segment_count = 0u;
    DIC_EXPECT(dic_j2k_packet_header_append_bit(&header, 1u) == DIC_STATUS_OK);
    DIC_EXPECT(dic_j2k_packet_header_append_codeblock(&header, &codeblock) == DIC_STATUS_OK);
    DIC_EXPECT(dic_j2k_packet_header_finish(&header) == DIC_STATUS_OK);
    DIC_EXPECT(header.size > 0u);
    dic_j2k_packet_header_free(&header);

    dic_j2k_packet_header_init(&nonempty_payload);
    DIC_EXPECT(dic_j2k_packet_build_single_codeblock_payload(
        &codeblock,
        codeword,
        sizeof(codeword),
        &nonempty_payload
    ) == DIC_STATUS_OK);
    DIC_EXPECT(nonempty_payload.size > sizeof(codeword));
    DIC_EXPECT(nonempty_payload.data[nonempty_payload.size - 3u] == 0x12u);
    DIC_EXPECT(nonempty_payload.data[nonempty_payload.size - 2u] == 0x34u);
    DIC_EXPECT(nonempty_payload.data[nonempty_payload.size - 1u] == 0x56u);
    dic_j2k_packet_header_free(&nonempty_payload);

    DIC_EXPECT(dic_j2k_packet_prepare_codeblock(&prepared, 0u, 9u, sizeof(codeword)) == DIC_STATUS_OK);
    DIC_EXPECT(prepared.included == 1);
    DIC_EXPECT(prepared.first_inclusion == 1);
    DIC_EXPECT(prepared.coding_passes == 9u);
    prepared.first_inclusion = 0;
    prepared.codeword_length = 31u;
    dic_j2k_packet_header_init(&header);
    DIC_EXPECT(dic_j2k_packet_header_append_codeblock(&header, &prepared) == DIC_STATUS_OK);
    DIC_EXPECT(dic_j2k_packet_header_finish(&header) == DIC_STATUS_OK);
    DIC_EXPECT(header.size == 3u);
    DIC_EXPECT(header.data[0] == 0xf8u);
    DIC_EXPECT(header.data[1] == 0xcfu);
    DIC_EXPECT(header.data[2] == 0x80u);
    dic_j2k_packet_header_free(&header);
    prepared.first_inclusion = 1;
    prepared.codeword_length = (uint32_t)sizeof(codeword);
    contributions[0].header = prepared;
    contributions[0].codeword = codeword;
    contributions[0].codeword_size = sizeof(codeword);
    DIC_EXPECT(dic_j2k_packet_prepare_codeblock(&prepared, 1u, 2u, sizeof(second_codeword)) == DIC_STATUS_OK);
    contributions[1].header = prepared;
    contributions[1].codeword = second_codeword;
    contributions[1].codeword_size = sizeof(second_codeword);
    dic_j2k_packet_header_init(&nonempty_payload);
    DIC_EXPECT(dic_j2k_packet_build_codeblock_payload(contributions, 2u, &nonempty_payload) == DIC_STATUS_OK);
    DIC_EXPECT(nonempty_payload.size > sizeof(codeword) + sizeof(second_codeword));
    DIC_EXPECT(nonempty_payload.data[nonempty_payload.size - 5u] == 0x12u);
    DIC_EXPECT(nonempty_payload.data[nonempty_payload.size - 4u] == 0x34u);
    DIC_EXPECT(nonempty_payload.data[nonempty_payload.size - 3u] == 0x56u);
    DIC_EXPECT(nonempty_payload.data[nonempty_payload.size - 2u] == 0xabu);
    DIC_EXPECT(nonempty_payload.data[nonempty_payload.size - 1u] == 0xcdu);
    dic_j2k_packet_header_free(&nonempty_payload);

    DIC_EXPECT(dic_j2k_packet_build_empty_lrcp_payload(3u, 5u, 1u, &payload) == DIC_STATUS_OK);
    DIC_EXPECT(payload.size == 18u);
    for (i = 0u; i < payload.size; ++i)
        DIC_EXPECT(payload.data[i] == 0x00u);
    dic_j2k_packet_header_free(&payload);

    for (i = 0u; i < 3u; ++i)
    {
        tagged_streams[i].mq.data = tagged_codewords + i;
        tagged_streams[i].mq.byte_count = 1u;
        tagged_streams[i].coding_passes = 1u;
    }
    tagged_streams[0].zero_bitplanes = 1u;
    tagged_streams[1].zero_bitplanes = 3u;
    tagged_streams[2].zero_bitplanes = 2u;
    tagged_subband.streams = tagged_streams;
    tagged_subband.stream_count = 3u;
    tagged_subband.blocks_x = 3;
    tagged_subband.blocks_y = 1;
    dic_j2k_packet_header_init(&payload);
    DIC_EXPECT(dic_j2k_packet_build_tagged_ebcot_payload(&tagged_subband, 1u, &payload) == DIC_STATUS_OK);
    DIC_EXPECT(payload.size == 7u);
    DIC_EXPECT(payload.data[0] == 0xf7u);
    DIC_EXPECT(payload.data[1] == 0x0cu);
    DIC_EXPECT(payload.data[2] == 0x87u);
    DIC_EXPECT(payload.data[3] == 0x61u);
    DIC_EXPECT(payload.data[4] == 0x11u);
    DIC_EXPECT(payload.data[5] == 0x22u);
    DIC_EXPECT(payload.data[6] == 0x33u);
    dic_j2k_packet_header_free(&payload);

    for (i = 0u; i < 4u; ++i)
    {
        precinct_streams[i].mq.data = precinct_codewords + i;
        precinct_streams[i].mq.byte_count = 1u;
        precinct_streams[i].coding_passes = 1u;
        precinct_streams[i].zero_bitplanes = i;
    }
    precinct_subband.streams = precinct_streams;
    precinct_subband.stream_count = 4u;
    precinct_subband.blocks_x = 4;
    precinct_subband.blocks_y = 1;
    precinct_subband.first_block_x = 2;
    precinct_subband.first_block_y = 0;
    precinct_subband.packet_blocks_x = 1;
    precinct_subband.packet_blocks_y = 1;
    precinct_parser_layout.blocks_x = 4;
    precinct_parser_layout.blocks_y = 1;
    precinct_parser_layout.first_block_x = 2;
    precinct_parser_layout.first_block_y = 0;
    precinct_parser_layout.packet_blocks_x = 1;
    precinct_parser_layout.packet_blocks_y = 1;
    DIC_EXPECT(dic_j2k_packet_header_parser_create(&precinct_parser_layout, 1u, 0, &parser) == DIC_STATUS_OK);
    dic_j2k_packet_header_init(&payload);
    DIC_EXPECT(dic_j2k_packet_build_tagged_ebcot_layer_payload_with_header_size(
        &precinct_subband,
        1u,
        0u,
        1u,
        &payload,
        &layer_header_size
    ) == DIC_STATUS_OK);
    DIC_EXPECT(payload.size == layer_header_size + 1u);
    DIC_EXPECT(payload.data[payload.size - 1u] == 0x43u);
    DIC_EXPECT(dic_j2k_packet_header_parser_parse(parser, payload.data, payload.size, 0u, &parse_result) == DIC_STATUS_OK);
    DIC_EXPECT(parse_result.range_count == 1u);
    DIC_EXPECT(parse_result.ranges[0].subband_index == 0u);
    DIC_EXPECT(parse_result.ranges[0].codeblock_index == 2u);
    DIC_EXPECT(parse_result.ranges[0].byte_offset == layer_header_size);
    DIC_EXPECT(parse_result.ranges[0].byte_count == 1u);
    dic_j2k_packet_header_free(&payload);
    dic_j2k_packet_header_parser_destroy(parser);
    parser = NULL;

    DIC_EXPECT(dic_j2k_ebcot_encode_codeblock_rect(
        coefficients,
        4u,
        4u,
        DIC_J2K_SUBBAND_LL_LH,
        &eb_stream
    ) == DIC_STATUS_OK);
    dic_j2k_packet_header_init(&payload);
    DIC_EXPECT(dic_j2k_packet_build_ebcot_payload(&eb_stream, 1u, &payload) == DIC_STATUS_OK);
    DIC_EXPECT(payload.size > eb_stream.mq.byte_count);
    for (i = 0u; i < eb_stream.mq.byte_count; ++i)
        DIC_EXPECT(payload.data[payload.size - eb_stream.mq.byte_count + i] == eb_stream.mq.data[i]);
    dic_j2k_packet_header_free(&payload);

    eb_subband.streams = &eb_stream;
    eb_subband.stream_count = 1u;
    eb_subband.blocks_x = 1;
    eb_subband.blocks_y = 1;
    parser_layout.blocks_x = 1;
    parser_layout.blocks_y = 1;
    DIC_EXPECT(dic_j2k_packet_header_parser_create(&parser_layout, 1u, 1, &parser) == DIC_STATUS_OK);

    dic_j2k_packet_header_init(&payload);
    DIC_EXPECT(dic_j2k_packet_build_tagged_ebcot_layer_payload_with_header_size(
        &eb_subband,
        1u,
        0u,
        2u,
        &payload,
        &layer_header_size
    ) == DIC_STATUS_OK);
    DIC_EXPECT(payload.size >= layer_header_size);
    first_layer_bytes = payload.size - layer_header_size;
    DIC_EXPECT(memcmp(payload.data + layer_header_size, eb_stream.mq.data, first_layer_bytes) == 0);
    DIC_EXPECT(dic_j2k_packet_header_parser_parse(parser, payload.data, payload.size, 0u, &parse_result) == DIC_STATUS_OK);
    DIC_EXPECT(parse_result.is_empty == 0);
    DIC_EXPECT(parse_result.packet_header_size == layer_header_size);
    DIC_EXPECT(parse_result.packet_body_size == first_layer_bytes);
    expected_offset = layer_header_size;
    parsed_bytes = 0u;
    for (i = 0u; i < parse_result.range_count; ++i)
    {
        DIC_EXPECT(parse_result.ranges[i].subband_index == 0u);
        DIC_EXPECT(parse_result.ranges[i].codeblock_index == 0u);
        DIC_EXPECT(parse_result.ranges[i].pass_index == i);
        DIC_EXPECT(parse_result.ranges[i].byte_offset == expected_offset);
        DIC_EXPECT(parse_result.ranges[i].byte_count == eb_stream.pass_lengths[i]);
        expected_offset += parse_result.ranges[i].byte_count;
        parsed_bytes += parse_result.ranges[i].byte_count;
    }
    DIC_EXPECT(parsed_bytes == first_layer_bytes);
    dic_j2k_packet_header_free(&payload);
    dic_j2k_packet_header_init(&payload);
    DIC_EXPECT(dic_j2k_packet_build_tagged_ebcot_layer_payload_with_header_size(
        &eb_subband,
        1u,
        1u,
        2u,
        &payload,
        &layer_header_size
    ) == DIC_STATUS_OK);
    second_layer_bytes = eb_stream.mq.byte_count - first_layer_bytes;
    DIC_EXPECT(payload.size == layer_header_size + second_layer_bytes);
    DIC_EXPECT(memcmp(payload.data + layer_header_size, eb_stream.mq.data + first_layer_bytes, second_layer_bytes) == 0);
    DIC_EXPECT(dic_j2k_packet_header_parser_parse(parser, payload.data, payload.size, 1u, &parse_result) == DIC_STATUS_OK);
    DIC_EXPECT(parse_result.is_empty == 0);
    DIC_EXPECT(parse_result.packet_header_size == layer_header_size);
    DIC_EXPECT(parse_result.packet_body_size == second_layer_bytes);
    expected_offset = layer_header_size;
    parsed_bytes = 0u;
    for (i = 0u; i < parse_result.range_count; ++i)
    {
        uint32_t pass_index = parse_result.ranges[i].pass_index;

        DIC_EXPECT(parse_result.ranges[i].subband_index == 0u);
        DIC_EXPECT(parse_result.ranges[i].codeblock_index == 0u);
        DIC_EXPECT(pass_index < eb_stream.coding_passes);
        DIC_EXPECT(parse_result.ranges[i].byte_offset == expected_offset);
        DIC_EXPECT(parse_result.ranges[i].byte_count == eb_stream.pass_lengths[pass_index]);
        expected_offset += parse_result.ranges[i].byte_count;
        parsed_bytes += parse_result.ranges[i].byte_count;
    }
    DIC_EXPECT(parsed_bytes == second_layer_bytes);
    dic_j2k_packet_header_free(&payload);
    dic_j2k_packet_parse_result_free(&parse_result);
    dic_j2k_packet_header_parser_destroy(parser);
    dic_j2k_codeblock_stream_free(&eb_stream);

    return 0;
}

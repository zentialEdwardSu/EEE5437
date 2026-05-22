#include "j2k/dic_j2k_packet.h"
#include "test_helpers.h"

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
    dic_j2k_packet_subband_payload tagged_subband;
    const uint8_t codeword[] = {0x12u, 0x34u, 0x56u};
    const uint8_t second_codeword[] = {0xabu, 0xcdu};
    uint8_t tagged_codewords[] = {0x11u, 0x22u, 0x33u};
    const int32_t coefficients[] = {
        0, 7, -2, 0,
        5, 0, 0, -9,
        0, 1, -1, 0,
        4, 0, 0, 3
    };
    dic_j2k_codeblock_stream eb_stream;
    unsigned int i;
    uint32_t increment;

    dic_j2k_packet_header_init(&header);
    dic_j2k_packet_header_init(&payload);
    dic_j2k_codeblock_stream_init(&eb_stream);
    for (i = 0u; i < 3u; ++i)
        dic_j2k_codeblock_stream_init(tagged_streams + i);

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
    codeblock.lblock_increment = 1u;
    codeblock.codeword_length = 3u;
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
    dic_j2k_codeblock_stream_free(&eb_stream);

    return 0;
}

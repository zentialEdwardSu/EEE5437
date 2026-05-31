#include "j2k/j2k_ebcot.h"
#include "test_helpers.h"

#include <string.h>

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.6-B.10.8 and Annex D.3, a full code-block contribution emits the first cleanup pass followed by three passes for each lower bit-plane. */
int main(void)
{
    const int32_t coefficients[] = {0, -1, 2, -3, 17, -31, 64, 5};
    const int32_t rect_coefficients[] = {
        0, 0, 3, -5,
        2, -9, 0, 1,
        0, 4, -7, 0,
        11, 0, -1, 6,
        0, 0, 0, -13
    };
    const int32_t zero_coefficients[6] = {0};
    int32_t decoded[sizeof(coefficients) / sizeof(coefficients[0])];
    int32_t rect_decoded[sizeof(rect_coefficients) / sizeof(rect_coefficients[0])];
    int32_t zero_decoded[sizeof(zero_coefficients) / sizeof(zero_coefficients[0])];
    j2k_codeblock_stream stream;
    j2k_codeblock_stream rect_stream;
    j2k_codeblock_stream zero_stream;
    size_t pass_length_sum;
    size_t pass_decision_sum;
    uint32_t pass;

    j2k_codeblock_stream_init(&stream);
    j2k_codeblock_stream_init(&rect_stream);
    j2k_codeblock_stream_init(&zero_stream);
    DIC_EXPECT(j2k_ebcot_encode_codeblock(
        coefficients,
        sizeof(coefficients) / sizeof(coefficients[0]),
        &stream
    ) == DIC_STATUS_OK);
    DIC_EXPECT(stream.coding_passes == 19u);
    DIC_EXPECT(stream.mq.byte_count > 0u);
    DIC_EXPECT(stream.magnitude_bitplanes > 0u);
    DIC_EXPECT(stream.pass_lengths != NULL);
    DIC_EXPECT(stream.pass_decision_counts != NULL);
    DIC_EXPECT(stream.pass_distortion_reductions != NULL);
    DIC_EXPECT(stream.pass_rd_slopes != NULL);
    pass_length_sum = 0u;
    pass_decision_sum = 0u;
    for (pass = 0u; pass < stream.coding_passes; ++pass)
    {
        pass_length_sum += stream.pass_lengths[pass];
        pass_decision_sum += stream.pass_decision_counts[pass];
        DIC_EXPECT(stream.pass_distortion_reductions[pass] >= 0.0);
        DIC_EXPECT(stream.pass_rd_slopes[pass] >= 0.0);
    }
    DIC_EXPECT(pass_length_sum == stream.mq.byte_count);
    DIC_EXPECT(pass_decision_sum == stream.mq.bit_count);
    DIC_EXPECT(j2k_ebcot_decode_codeblock(
        &stream,
        sizeof(coefficients) / sizeof(coefficients[0]),
        decoded
    ) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(decoded, coefficients, sizeof(coefficients)) == 0);
    j2k_codeblock_stream_free(&stream);

    DIC_EXPECT(j2k_ebcot_encode_codeblock_rect(
        rect_coefficients,
        4u,
        5u,
        j2k_SUBBAND_HH,
        &rect_stream
    ) == DIC_STATUS_OK);
    DIC_EXPECT(rect_stream.width == 4u);
    DIC_EXPECT(rect_stream.height == 5u);
    DIC_EXPECT(rect_stream.subband_orientation == j2k_SUBBAND_HH);
    DIC_EXPECT(rect_stream.coding_passes == 10u);
    DIC_EXPECT(rect_stream.magnitude_bitplanes > 0u);
    DIC_EXPECT(rect_stream.mq.byte_count > 0u);
    DIC_EXPECT(rect_stream.pass_lengths != NULL);
    DIC_EXPECT(j2k_ebcot_decode_codeblock_rect(
        &rect_stream,
        4u,
        5u,
        j2k_SUBBAND_HH,
        rect_decoded
    ) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(rect_decoded, rect_coefficients, sizeof(rect_coefficients)) == 0);
    j2k_codeblock_stream_free(&rect_stream);

    DIC_EXPECT(j2k_ebcot_encode_codeblock(
        zero_coefficients,
        sizeof(zero_coefficients) / sizeof(zero_coefficients[0]),
        &zero_stream
    ) == DIC_STATUS_OK);
    DIC_EXPECT(zero_stream.coding_passes == 0u);
    DIC_EXPECT(zero_stream.mq.byte_count == 0u);
    DIC_EXPECT(zero_stream.pass_lengths == NULL);
    DIC_EXPECT(j2k_ebcot_decode_codeblock(
        &zero_stream,
        sizeof(zero_coefficients) / sizeof(zero_coefficients[0]),
        zero_decoded
    ) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(zero_decoded, zero_coefficients, sizeof(zero_coefficients)) == 0);
    j2k_codeblock_stream_free(&zero_stream);

    return 0;
}

#include <string.h>

#include "j2k/dic_j2k_mq.h"
#include "test_helpers.h"

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3-C.4, MQ encoder/decoder preserve the binary decision sequence. */
int main(void)
{
    const uint8_t contexts[] = {0, 1, 1, 2, 3, 3, 4, 5, 5, 6};
    const uint8_t decisions[] = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1};
    uint8_t biased_contexts[128];
    uint8_t biased_decisions[128];
    uint8_t stress_contexts[1024];
    uint8_t stress_decisions[1024];
    uint8_t custom_contexts[32];
    uint8_t custom_decisions[32];
    uint8_t decoded[sizeof(decisions) / sizeof(decisions[0])];
    uint8_t biased_decoded[sizeof(biased_decisions) / sizeof(biased_decisions[0])];
    uint8_t stress_decoded[sizeof(stress_decisions) / sizeof(stress_decisions[0])];
    uint8_t custom_decoded[sizeof(custom_decisions) / sizeof(custom_decisions[0])];
    dic_j2k_mq_stream stream;
    dic_j2k_mq_stream biased_stream;
    dic_j2k_mq_stream stress_stream;
    dic_j2k_mq_stream custom_stream;
    size_t i;
    dic_j2k_mq_context_state state;
    dic_j2k_mq_context_state initial_states[19];
    dic_j2k_mq_registers registers;
    const dic_j2k_mq_table_entry *table = dic_j2k_mq_table();

    dic_j2k_mq_stream_init(&stream);
    dic_j2k_mq_stream_init(&biased_stream);
    dic_j2k_mq_stream_init(&stress_stream);
    dic_j2k_mq_stream_init(&custom_stream);
    DIC_EXPECT(dic_j2k_mq_table_size() == 47u);
    DIC_EXPECT(table[0].qe == 0x5601u);
    DIC_EXPECT(table[0].nmps == 1u);
    DIC_EXPECT(table[0].nlps == 1u);
    DIC_EXPECT(table[0].switch_mps == 1u);
    DIC_EXPECT(table[46].qe == 0x5601u);
    DIC_EXPECT(table[46].nmps == 46u);
    DIC_EXPECT(table[46].nlps == 46u);

    DIC_EXPECT(dic_j2k_mq_initenc_registers(&registers, 0u) == DIC_STATUS_OK);
    DIC_EXPECT(registers.a == 0x8000u);
    DIC_EXPECT(registers.c == 0u);
    DIC_EXPECT(registers.ct == 12u);
    DIC_EXPECT(dic_j2k_mq_initenc_registers(&registers, 0xffu) == DIC_STATUS_OK);
    DIC_EXPECT(registers.a == 0x8000u);
    DIC_EXPECT(registers.c == 0u);
    DIC_EXPECT(registers.ct == 13u);

    DIC_EXPECT(dic_j2k_mq_contexts_init(&state, 1u) == DIC_STATUS_OK);
    DIC_EXPECT(state.index == 0u);
    DIC_EXPECT(state.mps == 0u);
    DIC_EXPECT(dic_j2k_mq_context_update(&state, 0u) == DIC_STATUS_OK);
    DIC_EXPECT(state.index == 1u);
    DIC_EXPECT(state.mps == 0u);
    DIC_EXPECT(dic_j2k_mq_context_update(&state, 1u) == DIC_STATUS_OK);
    DIC_EXPECT(state.index == 6u);

    DIC_EXPECT(dic_j2k_mq_encode_decisions(
        contexts,
        decisions,
        sizeof(decisions) / sizeof(decisions[0]),
        &stream
    ) == DIC_STATUS_OK);
    DIC_EXPECT(stream.byte_count > 0u);
    DIC_EXPECT(stream.bit_count == sizeof(decisions) / sizeof(decisions[0]));
    DIC_EXPECT(dic_j2k_mq_decode_decisions(
        &stream,
        contexts,
        sizeof(decoded) / sizeof(decoded[0]),
        decoded
    ) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(decoded, decisions, sizeof(decoded)) == 0);
    dic_j2k_mq_stream_free(&stream);

    for (i = 0u; i < sizeof(biased_decisions); ++i)
    {
        biased_contexts[i] = 9u;
        biased_decisions[i] = 0u;
    }
    DIC_EXPECT(dic_j2k_mq_encode_decisions(
        biased_contexts,
        biased_decisions,
        sizeof(biased_decisions),
        &biased_stream
    ) == DIC_STATUS_OK);
    DIC_EXPECT(biased_stream.byte_count < (sizeof(biased_decisions) + 7u) / 8u);
    DIC_EXPECT(dic_j2k_mq_decode_decisions(
        &biased_stream,
        biased_contexts,
        sizeof(biased_decoded),
        biased_decoded
    ) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(biased_decoded, biased_decisions, sizeof(biased_decoded)) == 0);
    dic_j2k_mq_stream_free(&biased_stream);

    for (i = 0u; i < sizeof(stress_decisions); ++i)
    {
        stress_contexts[i] = (uint8_t)((i * 17u + 3u) & 31u);
        stress_decisions[i] = (uint8_t)(((i * 73u + (i >> 2)) >> 3u) & 1u);
    }
    DIC_EXPECT(dic_j2k_mq_encode_decisions(
        stress_contexts,
        stress_decisions,
        sizeof(stress_decisions),
        &stress_stream
    ) == DIC_STATUS_OK);
    for (i = 0u; i + 1u < stress_stream.byte_count; ++i)
    {
        if (stress_stream.data[i] == 0xffu)
            DIC_EXPECT((stress_stream.data[i + 1u] & 0x80u) == 0u);
    }
    DIC_EXPECT(dic_j2k_mq_decode_decisions(
        &stress_stream,
        stress_contexts,
        sizeof(stress_decoded),
        stress_decoded
    ) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(stress_decoded, stress_decisions, sizeof(stress_decoded)) == 0);
    dic_j2k_mq_stream_free(&stress_stream);

    DIC_EXPECT(dic_j2k_mq_contexts_init(initial_states, sizeof(initial_states) / sizeof(initial_states[0])) == DIC_STATUS_OK);
    initial_states[0].index = 4u;
    initial_states[17].index = 3u;
    initial_states[18].index = 46u;
    for (i = 0u; i < sizeof(custom_decisions); ++i)
    {
        custom_contexts[i] = (uint8_t)(i % 3u == 0u ? 18u : (i % 5u == 0u ? 17u : 0u));
        custom_decisions[i] = (uint8_t)((i * 5u + 1u) & 1u);
    }
    DIC_EXPECT(dic_j2k_mq_encode_decisions_with_states(
        initial_states,
        sizeof(initial_states) / sizeof(initial_states[0]),
        custom_contexts,
        custom_decisions,
        sizeof(custom_decisions),
        &custom_stream
    ) == DIC_STATUS_OK);
    DIC_EXPECT(dic_j2k_mq_decode_decisions_with_states(
        &custom_stream,
        initial_states,
        sizeof(initial_states) / sizeof(initial_states[0]),
        custom_contexts,
        sizeof(custom_decoded),
        custom_decoded
    ) == DIC_STATUS_OK);
    DIC_EXPECT(memcmp(custom_decoded, custom_decisions, sizeof(custom_decoded)) == 0);
    dic_j2k_mq_stream_free(&custom_stream);

    return 0;
}

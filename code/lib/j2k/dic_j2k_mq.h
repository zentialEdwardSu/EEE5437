#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_j2k_mq_stream
{
    uint8_t *data; /**< Encoded MQ byte stream. */
    size_t byte_count; /**< Number of valid bytes in data. */
    size_t bit_count; /**< Number of context decisions represented by the stream. */
} dic_j2k_mq_stream;

typedef struct dic_j2k_mq_context_state
{
    uint8_t index; /**< Probability-estimator state index into the Annex C Qe table. */
    uint8_t mps; /**< Current most-probable-symbol value for this context. */
} dic_j2k_mq_context_state;

typedef struct dic_j2k_mq_table_entry
{
    uint16_t qe; /**< Qe probability estimate from Annex C Table C.2. */
    uint8_t nmps; /**< Next state index after coding or decoding an MPS. */
    uint8_t nlps; /**< Next state index after coding or decoding an LPS. */
    uint8_t switch_mps; /**< Non-zero when an LPS transition toggles the context MPS. */
} dic_j2k_mq_table_entry;

typedef struct dic_j2k_mq_registers
{
    uint32_t a; /**< MQ interval register A. */
    uint32_t c; /**< MQ code register C. */
    uint8_t ct; /**< MQ bit counter CT for byte-in/byte-out procedures. */
} dic_j2k_mq_registers;

typedef struct dic_j2k_mq_decoder_session
{
    const uint8_t *data; /**< Input MQ byte stream owned by the caller. */
    size_t size; /**< Number of bytes available in data. */
    size_t offset; /**< Next byte offset consumed by BYTEIN. */
    uint8_t byte; /**< Most recent byte loaded by the decoder. */
    dic_j2k_mq_registers registers; /**< Live Annex C decoder registers. */
    dic_j2k_mq_context_state states[256]; /**< Decoder context states indexed by context label. */
    size_t decisions_decoded; /**< Number of binary decisions decoded so far. */
    size_t decision_limit; /**< Maximum decisions expected from the stream. */
} dic_j2k_mq_decoder_session;

void dic_j2k_mq_stream_init(dic_j2k_mq_stream *stream);
void dic_j2k_mq_stream_free(dic_j2k_mq_stream *stream);

const dic_j2k_mq_table_entry *dic_j2k_mq_table(void);
size_t dic_j2k_mq_table_size(void);

dic_status dic_j2k_mq_initenc_registers(
    dic_j2k_mq_registers *registers,
    uint8_t preceding_byte
);

dic_status dic_j2k_mq_contexts_init(
    dic_j2k_mq_context_state *states,
    size_t state_count
);

dic_status dic_j2k_mq_context_update(
    dic_j2k_mq_context_state *state,
    uint8_t decision
);

dic_status dic_j2k_mq_encode_decisions(
    const uint8_t *contexts,
    const uint8_t *decisions,
    size_t decision_count,
    dic_j2k_mq_stream *stream
);

dic_status dic_j2k_mq_encode_decisions_with_states(
    const dic_j2k_mq_context_state *initial_states,
    size_t initial_state_count,
    const uint8_t *contexts,
    const uint8_t *decisions,
    size_t decision_count,
    dic_j2k_mq_stream *stream
);

dic_status dic_j2k_mq_decode_decisions(
    const dic_j2k_mq_stream *stream,
    const uint8_t *contexts,
    size_t decision_count,
    uint8_t *decisions
);

dic_status dic_j2k_mq_decode_decisions_with_states(
    const dic_j2k_mq_stream *stream,
    const dic_j2k_mq_context_state *initial_states,
    size_t initial_state_count,
    const uint8_t *contexts,
    size_t decision_count,
    uint8_t *decisions
);

dic_status dic_j2k_mq_decoder_session_init(
    dic_j2k_mq_decoder_session *session,
    const dic_j2k_mq_stream *stream,
    const dic_j2k_mq_context_state *initial_states,
    size_t initial_state_count
);

dic_status dic_j2k_mq_decoder_session_decode(
    dic_j2k_mq_decoder_session *session,
    uint8_t context,
    uint8_t *decision
);

#ifdef __cplusplus
}
#endif

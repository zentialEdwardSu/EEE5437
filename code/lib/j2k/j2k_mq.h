#pragma once

/**
 * @file j2k_mq.h
 * @brief JPEG 2000 MQ arithmetic coder declarations.
 *
 * Implements T.800 Annex C — the MQ adaptive binary arithmetic coder
 * used by EBCOT to encode bit-plane decisions. The MQ coder is a
 * table-driven multiplication-free arithmetic coder that operates on
 * a sequence of binary context/decision pairs (CX, D).
 *
 * Encoding procedure (Annex C.3):
 *   - INITENC (C.3.1): Initialise the interval register A = 0x8000,
 *     code register C = 0, bit counter CT = 12, and BP (pointer
 *     past the most recent 0xFF byte emitted by BYTEOUT). The
 *     preceding byte value selects the initial BP.
 *   - ENCODE (C.3.2): For each (CX, D) pair, look up the Qe and index
 *     from the probability table. If D = MPS(CX), A is reduced by Qe;
 *     if A < 0x8000, renormalise (shift A and C left, decrement CT,
 *     emit byte when CT = 0 via BYTEOUT). If D = LPS(CX), C += A - Qe,
 *     A = Qe, check for MPS/LPS swap, then renormalise.
 *   - BYTEOUT (C.3.2.1): Emit C bits as bytes with bit-stuffing —
 *     if the emitted byte is 0xFF, increment BP and skip the next
 *     bit-slot to prevent false marker codes.
 *   - FLUSH (C.3.2.3): Terminate by emitting remaining C bits padded
 *     to a byte boundary, placing any needed bits beyond BP.
 *
 * Decoding procedure (Annex C.4):
 *   - INITDEC (C.4.1): Load initial A and C values from the stream.
 *   - DECODE (C.4.2): Compare A - Qe with (C - filler) shifted left
 *     by CT bits. If (C_low << CT) < A - Qe, decode MPS; otherwise
 *     decode LPS with the LPS conditional exchange.
 *   - BYTEIN (C.4.2.1): Load bytes from the compressed stream,
 *     skipping stuffed bytes after 0xFF (unless followed by a
 *     non-zero byte, which is a marker).
 *
 * Probability table (Table C.2):
 *   47-entry Qe table with nmps (next state after MPS), nlps (next
 *   state after LPS), and switch flag (whether LPS toggles MPS).
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex C.2 (probability table)
 * - paper/T-REC-T.800-200208.pdf, Annex C.3 (encoding procedures)
 * - paper/T-REC-T.800-200208.pdf, Annex C.4 (decoding procedures)
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encoded MQ byte stream with optional metadata.
 *
 * The data buffer holds packed MQ bytes following bit-stuffing rules.
 * bit_count tracks the number of binary decisions that produced the
 * stream (used for decoder-side decision counting).
 */
typedef struct j2k_mq_stream
{
    /** Encoded MQ byte stream (caller-owned after encoding). */
    uint8_t *data;
    /** Number of valid bytes in data. */
    size_t byte_count;
    /** Number of context decisions represented by the stream. */
    size_t bit_count;
} j2k_mq_stream;

/**
 * @brief MQ probability-estimator context state (Annex C.2).
 *
 * Each of the 19 EBCOT contexts (plus any non-EBCOT contexts) holds
 * an index into the IEC table and the current MPS value. The index
 * starts at 0 for the uniform-probability initial state and is
 * updated by the probability-estimation state machine during coding.
 */
typedef struct j2k_mq_context_state
{
    /** Probability-estimator state index into the Annex C Qe table (0–46). */
    uint8_t index;
    /** Current most-probable-symbol value for this context (0 or 1). */
    uint8_t mps;
} j2k_mq_context_state;

/**
 * @brief One row of the MQ Qe probability-estimation table (Table C.2).
 *
 * Each entry defines the behaviour for a given context probability state:
 * Qe (the LPS probability estimate), the next state after an MPS or LPS
 * event, and whether an LPS event toggles the context MPS value.
 */
typedef struct j2k_mq_table_entry
{
    /** Qe probability estimate (15-bit unsigned, Annex C Table C.2). */
    uint16_t qe;
    /** Next state index after coding or decoding an MPS. */
    uint8_t nmps;
    /** Next state index after coding or decoding an LPS. */
    uint8_t nlps;
    /** Non-zero when an LPS transition toggles the context MPS. */
    uint8_t switch_mps;
} j2k_mq_table_entry;

/**
 * @brief MQ encoder/decoder register file (Annex C.3.1, C.4.1).
 *
 * Holds the live interval register A (16-bit), code register C (28-bit
 * for encoder, 32-bit for decoder), and bit counter CT. The bit-stream
 * pointer BP is managed separately by the higher-level stream logic.
 */
typedef struct j2k_mq_registers
{
    /** MQ interval register A (16-bit, initialised to 0x8000). */
    uint32_t a;
    /** MQ code register C (28 bits for encoder, 32 bits for decoder). */
    uint32_t c;
    /** MQ bit counter CT for byte-in/byte-out procedures (0–12). */
    uint8_t ct;
} j2k_mq_registers;

/**
 * @brief MQ decoder session state for single-decision decode.
 *
 * Provides a stateful decoder interface that decodes one binary decision
 * at a time. Uses Annex C.4 DECODE/BYTEIN procedures internally.
 */
typedef struct j2k_mq_decoder_session
{
    /** Input MQ byte stream owned by the caller. */
    const uint8_t *data;
    /** Number of bytes available in data. */
    size_t size;
    /** Next byte offset consumed by BYTEIN. */
    size_t offset;
    /** Most recent byte loaded by the decoder (buffer B from Annex C). */
    uint8_t byte;
    /** Live Annex C decoder registers (A, C, CT). */
    j2k_mq_registers registers;
    /** Decoder context states indexed by context label (19 for EBCOT). */
    j2k_mq_context_state states[256];
    /** Number of binary decisions decoded so far. */
    size_t decisions_decoded;
    /** Maximum decisions expected from the stream. */
    size_t decision_limit;
} j2k_mq_decoder_session;

/** @brief Initialise an MQ stream to an empty state. */
void j2k_mq_stream_init(j2k_mq_stream *stream);

/** @brief Free the MQ stream's byte buffer. */
void j2k_mq_stream_free(j2k_mq_stream *stream);

/**
 * @brief Return a pointer to the Annex C Table C.2 Qe probability table.
 *
 * The table has j2k_mq_table_size() entries (47 rows). Each row defines
 * Qe, next-MPS, next-LPS, and MPS-switch for one probability state.
 *
 * @return Pointer to the static read-only IEC table.
 */
const j2k_mq_table_entry *j2k_mq_table(void);

/**
 * @brief Return the number of entries in the Qe probability table (47).
 *
 * @return Static table size.
 */
size_t j2k_mq_table_size(void);

/**
 * @brief Initialise MQ encoder registers per Annex C.3.1 (INITENC).
 *
 * Sets A = 0x8000, C = 0, CT = 12. The @p preceding_byte determines
 * the initial BP (byte-stuffing pointer). When @p preceding_byte is
 * 0xFF, BP = 1 (the second emitted byte acts as the stuff byte); for
 * any other value, BP = 0.
 *
 * @param registers [out] Receives initialised registers.
 * @param preceding_byte Value of the byte immediately before the first
 *                      MQ coder byte (typically 0x00 for a fresh stream).
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p registers is NULL.
 */
dic_status j2k_mq_initenc_registers(
    j2k_mq_registers *registers,
    uint8_t preceding_byte
);

/**
 * @brief Initialise MQ context states to the uniform-probability state.
 *
 * Sets each context index to 0 and MPS to 0 (per Annex C, the initial
 * state is the 0x5601 entry from Table C.2).
 *
 * @param states Array of context states to initialise.
 * @param state_count Number of entries in @p states.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p states is NULL and @p state_count > 0.
 */
dic_status j2k_mq_contexts_init(
    j2k_mq_context_state *states,
    size_t state_count
);

/**
 * @brief Update a single MQ context state after one decision.
 *
 * Follows the Annex C.2 probability-estimation state machine:
 * if decision == MPS, transition to nmps; if decision == LPS and
 * switch is set, toggle MPS and transition to nlps; otherwise
 * transition to nlps.
 *
 * @param state Context state to update in-place.
 * @param decision 0 (MPS) or 1 (LPS).
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_mq_context_update(
    j2k_mq_context_state *state,
    uint8_t decision
);

/**
 * @brief Encode a sequence of context/decision pairs into an MQ byte stream.
 *
 * Uses fresh initial context states (uniform distribution).
 *
 * @param contexts Array of context labels (CX) for each decision.
 * @param decisions Array of binary decisions (D).
 * @param decision_count Number of (CX, D) pairs.
 * @param stream [out] Receives the MQ-encoded byte stream.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_mq_encode_decisions(
    const uint8_t *contexts,
    const uint8_t *decisions,
    size_t decision_count,
    j2k_mq_stream *stream
);

/**
 * @brief Encode decisions with caller-supplied initial context states.
 *
 * @param initial_states Array of initial context states.
 * @param initial_state_count Number of states in @p initial_states.
 * @param contexts Context labels for each decision.
 * @param decisions Binary decisions.
 * @param decision_count Number of decisions.
 * @param stream [out] Receives the MQ-encoded byte stream.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_mq_encode_decisions_with_states(
    const j2k_mq_context_state *initial_states,
    size_t initial_state_count,
    const uint8_t *contexts,
    const uint8_t *decisions,
    size_t decision_count,
    j2k_mq_stream *stream
);

/**
 * @brief Encode decisions and return the final context states.
 *
 * Same as j2k_mq_encode_decisions_with_states() but also writes the
 * evolved context states into @p final_states for use in subsequent
 * encoding passes.
 *
 * @param initial_states Input context states.
 * @param initial_state_count Number of input states.
 * @param contexts Context labels per decision.
 * @param decisions Binary decisions per context.
 * @param decision_count Number of decisions.
 * @param stream [out] MQ-encoded byte stream.
 * @param final_states [out] Receives the evolved context states.
 * @param final_state_count Number of entries in @p final_states.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_mq_encode_decisions_with_state_result(
    const j2k_mq_context_state *initial_states,
    size_t initial_state_count,
    const uint8_t *contexts,
    const uint8_t *decisions,
    size_t decision_count,
    j2k_mq_stream *stream,
    j2k_mq_context_state *final_states,
    size_t final_state_count
);

/**
 * @brief Decode a sequence of context/decision pairs from an MQ stream.
 *
 * Decodes the exact number of decisions specified by the stream's
 * bit_count field. Uses fresh initial context states.
 *
 * @param stream MQ-encoded stream to decode from.
 * @param contexts Context labels for each decision to decode.
 * @param decision_count Number of decisions to decode.
 * @param decisions [out] Receives the decoded binary decisions.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_mq_decode_decisions(
    const j2k_mq_stream *stream,
    const uint8_t *contexts,
    size_t decision_count,
    uint8_t *decisions
);

/**
 * @brief Decode decisions with caller-supplied initial context states.
 *
 * @param stream MQ-encoded stream.
 * @param initial_states Pre-initialised context states.
 * @param initial_state_count Number of states.
 * @param contexts Context labels per decision.
 * @param decision_count Number of decisions.
 * @param decisions [out] Decoded binary decisions.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_mq_decode_decisions_with_states(
    const j2k_mq_stream *stream,
    const j2k_mq_context_state *initial_states,
    size_t initial_state_count,
    const uint8_t *contexts,
    size_t decision_count,
    uint8_t *decisions
);

/**
 * @brief Initialise a stateful MQ decoder session.
 *
 * Sets up the decoder registers from the stream's first two bytes
 * (Annex C.4.1 INITDEC), copies the initial context states, and
 * prepares for single-decision decoding via
 * j2k_mq_decoder_session_decode().
 *
 * @param session [out] Receives the initialised session.
 * @param stream Source MQ byte stream.
 * @param initial_states Initial context states to copy.
 * @param initial_state_count Number of context states.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_mq_decoder_session_init(
    j2k_mq_decoder_session *session,
    const j2k_mq_stream *stream,
    const j2k_mq_context_state *initial_states,
    size_t initial_state_count
);

/**
 * @brief Decode one binary decision from a stateful MQ session.
 *
 * Implements Annex C.4.2 DECODE with BYTEIN (C.4.2.1). Compares
 * the interval register against the MPS/LPS partition and updates
 * the context state. The session tracks decoded decision count.
 *
 * @param session Decoder session initialised by j2k_mq_decoder_session_init().
 * @param context Context label (CX) for this decision.
 * @param decision [out] Receives the decoded MPS/LPS decision.
 * @return DIC_STATUS_OK on success (one decision decoded).
 * @return DIC_J2K_FORMAT_ERROR if the stream is exhausted or malformed.
 */
dic_status j2k_mq_decoder_session_decode(
    j2k_mq_decoder_session *session,
    uint8_t context,
    uint8_t *decision
);

#ifdef __cplusplus
}
#endif

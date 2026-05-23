/**
 * @file dic_j2k_mq.c
 * @brief Implements the JPEG 2000 MQ arithmetic coder from T.800 Annex C with Annex J decoder guidance.
 *
 * The file contains the Qe state table, context state transitions, byte stuffing, compact
 * decision-stream encoding, and a session decoder used by EBCOT. The public helpers expose
 * a decision-oriented interface instead of a raw codestream parser, which keeps the project
 * tests focused while preserving Annex C/J state-machine behavior.
 *
 * References: dic_j2k_mq.h for stream/session structures, dic_j2k_ebcot.c for Annex D model
 * decisions, and Annex J.1/J.11 for software-convention decoder flow and arithmetic examples.
 */

#include "j2k/dic_j2k_mq.h"
#include "j2k/dic_j2k_debug.h"

#include <stdint.h>
#include <stdlib.h>

static const dic_j2k_mq_table_entry DIC_J2K_MQ_TABLE[] = {
    {0x5601u, 1u, 1u, 1u},
    {0x3401u, 2u, 6u, 0u},
    {0x1801u, 3u, 9u, 0u},
    {0x0ac1u, 4u, 12u, 0u},
    {0x0521u, 5u, 29u, 0u},
    {0x0221u, 38u, 33u, 0u},
    {0x5601u, 7u, 6u, 1u},
    {0x5401u, 8u, 14u, 0u},
    {0x4801u, 9u, 14u, 0u},
    {0x3801u, 10u, 14u, 0u},
    {0x3001u, 11u, 17u, 0u},
    {0x2401u, 12u, 18u, 0u},
    {0x1c01u, 13u, 20u, 0u},
    {0x1601u, 29u, 21u, 0u},
    {0x5601u, 15u, 14u, 1u},
    {0x5401u, 16u, 14u, 0u},
    {0x5101u, 17u, 15u, 0u},
    {0x4801u, 18u, 16u, 0u},
    {0x3801u, 19u, 17u, 0u},
    {0x3401u, 20u, 18u, 0u},
    {0x3001u, 21u, 19u, 0u},
    {0x2801u, 22u, 19u, 0u},
    {0x2401u, 23u, 20u, 0u},
    {0x2201u, 24u, 21u, 0u},
    {0x1c01u, 25u, 22u, 0u},
    {0x1801u, 26u, 23u, 0u},
    {0x1601u, 27u, 24u, 0u},
    {0x1401u, 28u, 25u, 0u},
    {0x1201u, 29u, 26u, 0u},
    {0x1101u, 30u, 27u, 0u},
    {0x0ac1u, 31u, 28u, 0u},
    {0x09c1u, 32u, 29u, 0u},
    {0x08a1u, 33u, 30u, 0u},
    {0x0521u, 34u, 31u, 0u},
    {0x0441u, 35u, 32u, 0u},
    {0x02a1u, 36u, 33u, 0u},
    {0x0221u, 37u, 34u, 0u},
    {0x0141u, 38u, 35u, 0u},
    {0x0111u, 39u, 36u, 0u},
    {0x0085u, 40u, 37u, 0u},
    {0x0049u, 41u, 38u, 0u},
    {0x0025u, 42u, 39u, 0u},
    {0x0015u, 43u, 40u, 0u},
    {0x0009u, 44u, 41u, 0u},
    {0x0005u, 45u, 42u, 0u},
    {0x0001u, 45u, 43u, 0u},
    {0x5601u, 46u, 46u, 0u}
};

enum
{
    DIC_J2K_MQ_REGISTER_A_INIT = 0x8000u,
    DIC_J2K_MQ_REGISTER_CT_INIT = 12u,
    DIC_J2K_MQ_REGISTER_CT_INIT_AFTER_FF = 13u
};

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3-C.4, MQ coding consumes context/decision pairs. */
void dic_j2k_mq_stream_init(dic_j2k_mq_stream *stream)
{
    DIC_J2K_DEBUG_ENTER();
    if (stream == NULL)
        return;
    stream->data = NULL;
    stream->byte_count = 0u;
    stream->bit_count = 0u;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3-C.4, MQ output is a byte stream carrying encoded binary decisions. */
void dic_j2k_mq_stream_free(dic_j2k_mq_stream *stream)
{
    DIC_J2K_DEBUG_ENTER();
    if (stream == NULL)
        return;
    free(stream->data);
    dic_j2k_mq_stream_init(stream);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Table C.2, Qe values and probability estimation state transitions. */
const dic_j2k_mq_table_entry *dic_j2k_mq_table(void)
{
    DIC_J2K_DEBUG_ENTER();
    return DIC_J2K_MQ_TABLE;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Table C.2, the MQ probability estimator has indexes 0 through 46. */
size_t dic_j2k_mq_table_size(void)
{
    DIC_J2K_DEBUG_ENTER();
    return sizeof(DIC_J2K_MQ_TABLE) / sizeof(DIC_J2K_MQ_TABLE[0]);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.8 Figure C.10, INITENC sets A=0x8000, C=0, and CT=12 or 13 after 0xFF. */
dic_status dic_j2k_mq_initenc_registers(
    dic_j2k_mq_registers *registers,
    uint8_t preceding_byte
)
{
    DIC_J2K_DEBUG_ENTER();
    if (registers == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    registers->a = DIC_J2K_MQ_REGISTER_A_INIT;
    registers->c = 0u;
    registers->ct = preceding_byte == 0xffu
        ? DIC_J2K_MQ_REGISTER_CT_INIT_AFTER_FF
        : DIC_J2K_MQ_REGISTER_CT_INIT;

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, C.2.8/C.3.5 and D.3 context modelling, contexts carry an index and MPS sense. */
dic_status dic_j2k_mq_contexts_init(
    dic_j2k_mq_context_state *states,
    size_t state_count
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;

    if (states == NULL && state_count > 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (index = 0u; index < state_count; ++index)
    {
        states[index].index = 0u;
        states[index].mps = 0u;
    }

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, C.2.4-C.2.5 and Table C.2, MPS/LPS decisions update the context state. */
dic_status dic_j2k_mq_context_update(
    dic_j2k_mq_context_state *state,
    uint8_t decision
)
{
    DIC_J2K_DEBUG_ENTER();
    const dic_j2k_mq_table_entry *entry;

    if (state == NULL || state->index >= dic_j2k_mq_table_size())
        return DIC_STATUS_INVALID_ARGUMENT;

    entry = DIC_J2K_MQ_TABLE + state->index;
    if ((decision & 1u) == state->mps)
    {
        state->index = entry->nmps;
    }
    else
    {
        if (entry->switch_mps)
            state->mps = (uint8_t)(1u - state->mps);
        state->index = entry->nlps;
    }

    return DIC_STATUS_OK;
}

typedef struct dic_j2k_mq_native_writer
{
    uint8_t *data;
    size_t size;
    size_t capacity;
    intptr_t bp;
    uint8_t sentinel_byte;
} dic_j2k_mq_native_writer;

typedef struct dic_j2k_mq_native_reader
{
    const uint8_t *data;
    size_t size;
    size_t offset;
    uint8_t byte;
} dic_j2k_mq_native_reader;

typedef struct dic_j2k_mq_native_encoder
{
    dic_j2k_mq_native_writer *writer;
    dic_j2k_mq_registers registers;
} dic_j2k_mq_native_encoder;

typedef struct dic_j2k_mq_native_decoder
{
    dic_j2k_mq_native_reader *reader;
    dic_j2k_mq_registers registers;
} dic_j2k_mq_native_decoder;

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.7 Figure C.9, BP initially addresses a byte before BPST. */
static void dic_j2k_mq_native_writer_init(dic_j2k_mq_native_writer *writer)
{
    DIC_J2K_DEBUG_ENTER();
    writer->data = NULL;
    writer->size = 0u;
    writer->capacity = 0u;
    writer->bp = -1;
    writer->sentinel_byte = 0u;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.9 Figure C.11, FLUSH completes the current compressed byte buffer. */
static void dic_j2k_mq_native_writer_free(dic_j2k_mq_native_writer *writer)
{
    DIC_J2K_DEBUG_ENTER();
    free(writer->data);
    dic_j2k_mq_native_writer_init(writer);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.7 Figure C.9, BYTEOUT advances BP before storing the next B byte. */
static dic_status dic_j2k_mq_native_writer_reserve(
    dic_j2k_mq_native_writer *writer,
    size_t needed
)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t *new_data;
    size_t new_capacity;

    if (needed <= writer->capacity)
        return DIC_STATUS_OK;

    new_capacity = writer->capacity == 0u ? 16u : writer->capacity;
    while (new_capacity < needed)
    {
        if (new_capacity > (size_t)-1 / 2u)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_capacity *= 2u;
    }

    new_data = (uint8_t *)realloc(writer->data, new_capacity);
    if (new_data == NULL)
        return DIC_STATUS_MEMORY_ERROR;
    writer->data = new_data;
    writer->capacity = new_capacity;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.7 Figure C.9, B is the byte addressed by BP. */
static uint8_t dic_j2k_mq_native_writer_b(const dic_j2k_mq_native_writer *writer)
{
    DIC_J2K_DEBUG_ENTER();
    if (writer->bp < 0)
        return writer->sentinel_byte;
    return writer->data[(size_t)writer->bp];
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.7 Figure C.9, carry propagation increments B before the next byte is emitted. */
static void dic_j2k_mq_native_writer_increment_b(dic_j2k_mq_native_writer *writer)
{
    DIC_J2K_DEBUG_ENTER();
    if (writer->bp < 0)
        ++writer->sentinel_byte;
    else
        ++writer->data[(size_t)writer->bp];
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.7 Figure C.9, BYTEOUT writes a new compressed image data byte. */
static dic_status dic_j2k_mq_native_writer_put_next_b(
    dic_j2k_mq_native_writer *writer,
    uint32_t value
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;
    dic_status status;

    if (writer->bp == INTPTR_MAX)
        return DIC_STATUS_INVALID_ARGUMENT;
    ++writer->bp;
    index = (size_t)writer->bp;
    status = dic_j2k_mq_native_writer_reserve(writer, index + 1u);
    if (status != DIC_STATUS_OK)
        return status;
    writer->data[index] = (uint8_t)(value & 0xffu);
    if (writer->size < index + 1u)
        writer->size = index + 1u;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.9 Figure C.11, a final non-0xFF B advances BP to the byte after the included codeword. */
static dic_status dic_j2k_mq_native_writer_finish(dic_j2k_mq_native_writer *writer)
{
    DIC_J2K_DEBUG_ENTER();
    if (writer == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (writer->bp >= 0 && dic_j2k_mq_native_writer_b(writer) != 0xffu)
    {
        if (writer->bp == INTPTR_MAX)
            return DIC_STATUS_INVALID_ARGUMENT;
        ++writer->bp;
    }
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.7-C.2.9, BP-BPST is the emitted codeword length after FLUSH. */
static size_t dic_j2k_mq_native_writer_byte_count(const dic_j2k_mq_native_writer *writer)
{
    DIC_J2K_DEBUG_ENTER();
    if (writer == NULL || writer->bp < 0)
        return 0u;
    return (size_t)writer->bp;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.7 Figure C.9, BYTEOUT handles carry and 0xFF bit stuffing from C. */
static dic_status dic_j2k_mq_native_byteout(dic_j2k_mq_native_encoder *encoder)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t b = dic_j2k_mq_native_writer_b(encoder->writer);

    if (b == 0xffu)
    {
        dic_status status = dic_j2k_mq_native_writer_put_next_b(
            encoder->writer,
            encoder->registers.c >> 20u
        );
        if (status != DIC_STATUS_OK)
            return status;
        encoder->registers.c &= 0xfffffu;
        encoder->registers.ct = 7u;
        return DIC_STATUS_OK;
    }

    if ((encoder->registers.c & 0x8000000u) != 0u)
    {
        dic_j2k_mq_native_writer_increment_b(encoder->writer);
        b = dic_j2k_mq_native_writer_b(encoder->writer);
        if (b == 0xffu)
        {
            dic_status status;

            encoder->registers.c &= 0x7ffffffu;
            status = dic_j2k_mq_native_writer_put_next_b(
                encoder->writer,
                encoder->registers.c >> 20u
            );
            if (status != DIC_STATUS_OK)
                return status;
            encoder->registers.c &= 0xfffffu;
            encoder->registers.ct = 7u;
            return DIC_STATUS_OK;
        }
    }

    {
        dic_status status = dic_j2k_mq_native_writer_put_next_b(
            encoder->writer,
            encoder->registers.c >> 19u
        );
        if (status != DIC_STATUS_OK)
            return status;
    }
    encoder->registers.c &= 0x7ffffu;
    encoder->registers.ct = 8u;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.6 Figure C.8, RENORME shifts A and C until A reaches 0x8000. */
static dic_status dic_j2k_mq_native_renorme(dic_j2k_mq_native_encoder *encoder)
{
    DIC_J2K_DEBUG_ENTER();
    do
    {
        encoder->registers.a <<= 1u;
        encoder->registers.c <<= 1u;
        --encoder->registers.ct;
        if (encoder->registers.ct == 0u)
        {
            dic_status status = dic_j2k_mq_native_byteout(encoder);
            if (status != DIC_STATUS_OK)
                return status;
        }
    } while ((encoder->registers.a & 0x8000u) == 0u);

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.5 Figure C.7, CODEMPS encodes the MPS sub-interval and NMPS update. */
static dic_status dic_j2k_mq_native_codemps(
    dic_j2k_mq_native_encoder *encoder,
    dic_j2k_mq_context_state *state
)
{
    DIC_J2K_DEBUG_ENTER();
    const dic_j2k_mq_table_entry *entry = DIC_J2K_MQ_TABLE + state->index;

    encoder->registers.a -= entry->qe;
    if ((encoder->registers.a & 0x8000u) != 0u)
    {
        encoder->registers.c += entry->qe;
        return DIC_STATUS_OK;
    }

    if (encoder->registers.a < entry->qe)
        encoder->registers.a = entry->qe;
    else
        encoder->registers.c += entry->qe;
    state->index = entry->nmps;
    return dic_j2k_mq_native_renorme(encoder);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.4 Figure C.6, CODELPS encodes the LPS path and NLPS/SWITCH update. */
static dic_status dic_j2k_mq_native_codelps(
    dic_j2k_mq_native_encoder *encoder,
    dic_j2k_mq_context_state *state
)
{
    DIC_J2K_DEBUG_ENTER();
    const dic_j2k_mq_table_entry *entry = DIC_J2K_MQ_TABLE + state->index;

    encoder->registers.a -= entry->qe;
    if (encoder->registers.a < entry->qe)
        encoder->registers.c += entry->qe;
    else
        encoder->registers.a = entry->qe;
    if (entry->switch_mps)
        state->mps = (uint8_t)(1u - state->mps);
    state->index = entry->nlps;
    return dic_j2k_mq_native_renorme(encoder);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.9 Figure C.12, SETBITS forces terminating one bits without crossing the interval bound. */
static void dic_j2k_mq_native_setbits(dic_j2k_mq_native_encoder *encoder)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t bound = encoder->registers.c + encoder->registers.a;

    encoder->registers.c |= 0xffffu;
    if (encoder->registers.c >= bound)
        encoder->registers.c -= 0x8000u;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2.9 Figure C.11, FLUSH sets terminating bits and emits the final MQ bytes. */
static dic_status dic_j2k_mq_native_flush(dic_j2k_mq_native_encoder *encoder)
{
    DIC_J2K_DEBUG_ENTER();
    dic_status status;

    dic_j2k_mq_native_setbits(encoder);
    encoder->registers.c <<= encoder->registers.ct;
    status = dic_j2k_mq_native_byteout(encoder);
    if (status != DIC_STATUS_OK)
        return status;
    encoder->registers.c <<= encoder->registers.ct;
    status = dic_j2k_mq_native_byteout(encoder);
    if (status != DIC_STATUS_OK)
        return status;
    return dic_j2k_mq_native_writer_finish(encoder->writer);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.4 Figure C.19, BYTEIN reads B and compensates for stuffed bits after 0xFF. */
static void dic_j2k_mq_native_bytein(dic_j2k_mq_native_decoder *decoder)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t b1;

    if (decoder->reader->byte == 0xffu)
    {
        b1 = decoder->reader->offset + 1u < decoder->reader->size
            ? decoder->reader->data[decoder->reader->offset + 1u]
            : 0xffu;
        if (b1 > 0x8fu)
        {
            decoder->registers.c += 0xff00u;
            decoder->registers.ct = 8u;
            return;
        }

        ++decoder->reader->offset;
        decoder->reader->byte = b1;
        decoder->registers.c += ((uint32_t)decoder->reader->byte) << 9u;
        decoder->registers.ct = 7u;
        return;
    }

    ++decoder->reader->offset;
    decoder->reader->byte = decoder->reader->offset < decoder->reader->size
        ? decoder->reader->data[decoder->reader->offset]
        : 0xffu;
    decoder->registers.c += ((uint32_t)decoder->reader->byte) << 8u;
    decoder->registers.ct = 8u;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.5 Figure C.20, INITDEC loads the first byte then aligns C by seven bits. */
static void dic_j2k_mq_native_initdec(
    dic_j2k_mq_native_decoder *decoder,
    dic_j2k_mq_native_reader *reader,
    const dic_j2k_mq_stream *stream
)
{
    DIC_J2K_DEBUG_ENTER();
    reader->data = stream->data;
    reader->size = stream->byte_count;
    reader->offset = 0u;
    reader->byte = stream->byte_count > 0u ? stream->data[0] : 0xffu;

    decoder->reader = reader;
    decoder->registers.a = DIC_J2K_MQ_REGISTER_A_INIT;
    decoder->registers.c = ((uint32_t)reader->byte) << 16u;
    decoder->registers.ct = 0u;
    dic_j2k_mq_native_bytein(decoder);
    decoder->registers.c <<= 7u;
    decoder->registers.ct = (uint8_t)(decoder->registers.ct - 7u);
    decoder->registers.a = DIC_J2K_MQ_REGISTER_A_INIT;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.3 Figure C.18, RENORMD shifts A and C and calls BYTEIN when CT is exhausted. */
static void dic_j2k_mq_native_renormd(dic_j2k_mq_native_decoder *decoder)
{
    DIC_J2K_DEBUG_ENTER();
    do
    {
        if (decoder->registers.ct == 0u)
            dic_j2k_mq_native_bytein(decoder);
        decoder->registers.a <<= 1u;
        decoder->registers.c <<= 1u;
        --decoder->registers.ct;
    } while ((decoder->registers.a & 0x8000u) == 0u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.2 Figure C.16, MPS_EXCHANGE applies the conditional MPS/LPS exchange. */
static uint8_t dic_j2k_mq_native_mps_exchange(
    dic_j2k_mq_native_decoder *decoder,
    dic_j2k_mq_context_state *state,
    const dic_j2k_mq_table_entry *entry
)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t decision;

    if (decoder->registers.a < entry->qe)
    {
        decision = (uint8_t)(1u - state->mps);
        if (entry->switch_mps)
            state->mps = (uint8_t)(1u - state->mps);
        state->index = entry->nlps;
    }
    else
    {
        decision = state->mps;
        state->index = entry->nmps;
    }
    return decision;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.2 Figure C.17, LPS_EXCHANGE selects the decoded decision and next state. */
static uint8_t dic_j2k_mq_native_lps_exchange(
    dic_j2k_mq_native_decoder *decoder,
    dic_j2k_mq_context_state *state,
    const dic_j2k_mq_table_entry *entry
)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t decision;

    if (decoder->registers.a < entry->qe)
    {
        decoder->registers.a = entry->qe;
        decision = state->mps;
        state->index = entry->nmps;
    }
    else
    {
        decoder->registers.a = entry->qe;
        decision = (uint8_t)(1u - state->mps);
        if (entry->switch_mps)
            state->mps = (uint8_t)(1u - state->mps);
        state->index = entry->nlps;
    }
    return decision;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.2 Figure C.15, DECODE compares Chigh with Qe and returns one binary decision. */
static dic_status dic_j2k_mq_native_decode(
    dic_j2k_mq_native_decoder *decoder,
    dic_j2k_mq_context_state *state,
    uint8_t *decision
)
{
    DIC_J2K_DEBUG_ENTER();
    const dic_j2k_mq_table_entry *entry = DIC_J2K_MQ_TABLE + state->index;

    decoder->registers.a -= entry->qe;
    if ((decoder->registers.c >> 16u) < entry->qe)
    {
        *decision = dic_j2k_mq_native_lps_exchange(decoder, state, entry);
        dic_j2k_mq_native_renormd(decoder);
        return DIC_STATUS_OK;
    }

    decoder->registers.c -= ((uint32_t)entry->qe) << 16u;
    if ((decoder->registers.a & 0x8000u) != 0u)
    {
        *decision = state->mps;
        return DIC_STATUS_OK;
    }

    *decision = dic_j2k_mq_native_mps_exchange(decoder, state, entry);
    dic_j2k_mq_native_renormd(decoder);
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.4 Figure C.19, streaming BYTEIN uses the same marker/stuff-bit rules as the bulk decoder. */
static void dic_j2k_mq_session_bytein(dic_j2k_mq_decoder_session *session)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t b1;

    if (session->byte == 0xffu)
    {
        b1 = session->offset + 1u < session->size
            ? session->data[session->offset + 1u]
            : 0xffu;
        if (b1 > 0x8fu)
        {
            session->registers.c += 0xff00u;
            session->registers.ct = 8u;
            return;
        }
        ++session->offset;
        session->byte = b1;
        session->registers.c += ((uint32_t)session->byte) << 9u;
        session->registers.ct = 7u;
        return;
    }

    ++session->offset;
    session->byte = session->offset < session->size ? session->data[session->offset] : 0xffu;
    session->registers.c += ((uint32_t)session->byte) << 8u;
    session->registers.ct = 8u;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.3 Figure C.18, streaming RENORMD consumes bytes as decisions are requested. */
static void dic_j2k_mq_session_renormd(dic_j2k_mq_decoder_session *session)
{
    DIC_J2K_DEBUG_ENTER();
    do
    {
        if (session->registers.ct == 0u)
            dic_j2k_mq_session_bytein(session);
        session->registers.a <<= 1u;
        session->registers.c <<= 1u;
        --session->registers.ct;
    } while ((session->registers.a & 0x8000u) == 0u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.2 Figure C.16, streaming MPS_EXCHANGE updates the addressed context state. */
static uint8_t dic_j2k_mq_session_mps_exchange(
    dic_j2k_mq_decoder_session *session,
    dic_j2k_mq_context_state *state,
    const dic_j2k_mq_table_entry *entry
)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t decision;

    if (session->registers.a < entry->qe)
    {
        decision = (uint8_t)(1u - state->mps);
        if (entry->switch_mps)
            state->mps = (uint8_t)(1u - state->mps);
        state->index = entry->nlps;
    }
    else
    {
        decision = state->mps;
        state->index = entry->nmps;
    }
    return decision;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.2 Figure C.17, streaming LPS_EXCHANGE mirrors the bulk decoder. */
static uint8_t dic_j2k_mq_session_lps_exchange(
    dic_j2k_mq_decoder_session *session,
    dic_j2k_mq_context_state *state,
    const dic_j2k_mq_table_entry *entry
)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t decision;

    if (session->registers.a < entry->qe)
    {
        session->registers.a = entry->qe;
        decision = state->mps;
        state->index = entry->nmps;
    }
    else
    {
        session->registers.a = entry->qe;
        decision = (uint8_t)(1u - state->mps);
        if (entry->switch_mps)
            state->mps = (uint8_t)(1u - state->mps);
        state->index = entry->nlps;
    }
    return decision;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.5 Figure C.20, streaming INITDEC initializes C, CT, A and caller-supplied context states. */
dic_status dic_j2k_mq_decoder_session_init(
    dic_j2k_mq_decoder_session *session,
    const dic_j2k_mq_stream *stream,
    const dic_j2k_mq_context_state *initial_states,
    size_t initial_state_count
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;

    if (session == NULL || stream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (initial_states == NULL && initial_state_count > 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (initial_state_count > sizeof(session->states) / sizeof(session->states[0]))
        return DIC_STATUS_INVALID_ARGUMENT;
    if (dic_j2k_mq_contexts_init(session->states, sizeof(session->states) / sizeof(session->states[0])) != DIC_STATUS_OK)
        return DIC_STATUS_INVALID_ARGUMENT;
    for (index = 0u; index < initial_state_count; ++index)
    {
        if (initial_states[index].index >= dic_j2k_mq_table_size())
            return DIC_STATUS_INVALID_ARGUMENT;
        session->states[index] = initial_states[index];
    }

    session->data = stream->data;
    session->size = stream->byte_count;
    session->offset = 0u;
    session->byte = stream->byte_count > 0u ? stream->data[0] : 0xffu;
    session->registers.a = DIC_J2K_MQ_REGISTER_A_INIT;
    session->registers.c = ((uint32_t)session->byte) << 16u;
    session->registers.ct = 0u;
    session->decisions_decoded = 0u;
    session->decision_limit = stream->bit_count;
    dic_j2k_mq_session_bytein(session);
    session->registers.c <<= 7u;
    session->registers.ct = (uint8_t)(session->registers.ct - 7u);
    session->registers.a = DIC_J2K_MQ_REGISTER_A_INIT;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3.2 Figure C.15, streaming DECODE returns one decision for the requested context. */
dic_status dic_j2k_mq_decoder_session_decode(
    dic_j2k_mq_decoder_session *session,
    uint8_t context,
    uint8_t *decision
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_mq_context_state *state;
    const dic_j2k_mq_table_entry *entry;

    if (session == NULL || decision == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (session->decisions_decoded >= session->decision_limit)
        return DIC_J2K_MALFORMED_ARITHMETIC_STREAM;

    state = session->states + context;
    if (state->index >= dic_j2k_mq_table_size())
        return DIC_STATUS_INVALID_ARGUMENT;
    entry = DIC_J2K_MQ_TABLE + state->index;

    session->registers.a -= entry->qe;
    if ((session->registers.c >> 16u) < entry->qe)
    {
        *decision = dic_j2k_mq_session_lps_exchange(session, state, entry);
        dic_j2k_mq_session_renormd(session);
    }
    else
    {
        session->registers.c -= ((uint32_t)entry->qe) << 16u;
        if ((session->registers.a & 0x8000u) != 0u)
        {
            *decision = state->mps;
        }
        else
        {
            *decision = dic_j2k_mq_session_mps_exchange(session, state, entry);
            dic_j2k_mq_session_renormd(session);
        }
    }
    ++session->decisions_decoded;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.3 Figures C.2-C.3, encoder input is an ordered sequence of CX,D pairs. */
dic_status dic_j2k_mq_encode_decisions(
    const uint8_t *contexts,
    const uint8_t *decisions,
    size_t decision_count,
    dic_j2k_mq_stream *stream
)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_mq_encode_decisions_with_states(
        NULL,
        0u,
        contexts,
        decisions,
        decision_count,
        stream
    );
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3 and Table D.7, coefficient coding may override the default MQ initial states. */
dic_status dic_j2k_mq_encode_decisions_with_states(
    const dic_j2k_mq_context_state *initial_states,
    size_t initial_state_count,
    const uint8_t *contexts,
    const uint8_t *decisions,
    size_t decision_count,
    dic_j2k_mq_stream *stream
)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_mq_encode_decisions_with_state_result(
        initial_states,
        initial_state_count,
        contexts,
        decisions,
        decision_count,
        stream,
        NULL,
        0u
    );
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.2, packet pass termination needs the true context states left by the MQ encoder. */
dic_status dic_j2k_mq_encode_decisions_with_state_result(
    const dic_j2k_mq_context_state *initial_states,
    size_t initial_state_count,
    const uint8_t *contexts,
    const uint8_t *decisions,
    size_t decision_count,
    dic_j2k_mq_stream *stream,
    dic_j2k_mq_context_state *final_states,
    size_t final_state_count
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;
    dic_j2k_mq_context_state states[256];
    dic_j2k_mq_native_writer writer;
    dic_j2k_mq_native_encoder encoder;

    if (contexts == NULL || decisions == NULL || stream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (final_states == NULL && final_state_count > 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (final_state_count > sizeof(states) / sizeof(states[0]))
        return DIC_STATUS_INVALID_ARGUMENT;
    if (initial_states == NULL && initial_state_count > 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (initial_state_count > sizeof(states) / sizeof(states[0]))
        return DIC_STATUS_INVALID_ARGUMENT;
    if (dic_j2k_mq_contexts_init(states, sizeof(states) / sizeof(states[0])) != DIC_STATUS_OK)
        return DIC_STATUS_INVALID_ARGUMENT;
    for (index = 0u; index < initial_state_count; ++index)
    {
        if (initial_states[index].index >= dic_j2k_mq_table_size())
            return DIC_STATUS_INVALID_ARGUMENT;
        states[index] = initial_states[index];
    }

    dic_j2k_mq_stream_free(stream);
    dic_j2k_mq_native_writer_init(&writer);
    encoder.writer = &writer;
    if (dic_j2k_mq_initenc_registers(&encoder.registers, 0u) != DIC_STATUS_OK)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (index = 0u; index < decision_count; ++index)
    {
        dic_status status;
        uint8_t cx = contexts[index];
        dic_j2k_mq_context_state *state = states + cx;

        if ((decisions[index] & 1u) == state->mps)
            status = dic_j2k_mq_native_codemps(&encoder, state);
        else
            status = dic_j2k_mq_native_codelps(&encoder, state);
        if (status != DIC_STATUS_OK)
        {
            dic_j2k_mq_native_writer_free(&writer);
            return status;
        }
    }

    if (dic_j2k_mq_native_flush(&encoder) != DIC_STATUS_OK)
    {
        dic_j2k_mq_native_writer_free(&writer);
        return DIC_STATUS_MEMORY_ERROR;
    }

    stream->data = writer.data;
    stream->byte_count = dic_j2k_mq_native_writer_byte_count(&writer);
    stream->bit_count = decision_count;
    for (index = 0u; index < final_state_count; ++index)
        final_states[index] = states[index];
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.4 Figure C.14-C.15, decoder reproduces the D sequence from the same context order. */
dic_status dic_j2k_mq_decode_decisions(
    const dic_j2k_mq_stream *stream,
    const uint8_t *contexts,
    size_t decision_count,
    uint8_t *decisions
)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_mq_decode_decisions_with_states(
        stream,
        NULL,
        0u,
        contexts,
        decision_count,
        decisions
    );
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C.4 and Table D.7, decoding must start from the same context states as encoding. */
dic_status dic_j2k_mq_decode_decisions_with_states(
    const dic_j2k_mq_stream *stream,
    const dic_j2k_mq_context_state *initial_states,
    size_t initial_state_count,
    const uint8_t *contexts,
    size_t decision_count,
    uint8_t *decisions
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;
    dic_j2k_mq_context_state states[256];
    dic_j2k_mq_native_reader reader;
    dic_j2k_mq_native_decoder decoder;

    if (stream == NULL || contexts == NULL || decisions == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (initial_states == NULL && initial_state_count > 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (initial_state_count > sizeof(states) / sizeof(states[0]))
        return DIC_STATUS_INVALID_ARGUMENT;
    if (decision_count > stream->bit_count)
        return DIC_J2K_MALFORMED_ARITHMETIC_STREAM;
    if (dic_j2k_mq_contexts_init(states, sizeof(states) / sizeof(states[0])) != DIC_STATUS_OK)
        return DIC_STATUS_INVALID_ARGUMENT;
    for (index = 0u; index < initial_state_count; ++index)
    {
        if (initial_states[index].index >= dic_j2k_mq_table_size())
            return DIC_STATUS_INVALID_ARGUMENT;
        states[index] = initial_states[index];
    }

    dic_j2k_mq_native_initdec(&decoder, &reader, stream);

    for (index = 0u; index < decision_count; ++index)
    {
        dic_status status;
        uint8_t cx = contexts[index];
        dic_j2k_mq_context_state *state = states + cx;

        status = dic_j2k_mq_native_decode(&decoder, state, decisions + index);
        if (status != DIC_STATUS_OK)
            return status;
    }

    return DIC_STATUS_OK;
}

/**
 * @file j2k_ebcot.c
 * @brief Implements JPEG 2000 EBCOT code-block modelling from T.800 Annex D.
 *
 * The encoder and decoder build significance, refinement, cleanup, sign, run-length, and
 * uniform-context decision streams for one code-block, then pass those decisions through
 * the Annex C MQ coder. Each coding pass is MQ-terminated independently and recorded in a
 * pass-length table, which gives packet code real truncation points for quality-layer
 * construction. Bypass, context reset, vertical causal, predictable termination, and
 * segmentation symbols remain fixed to the regular MQ-coded path.
 *
 * References: j2k_mq.c for Annex C MQ coding, j2k_packet.c for Annex B packet
 * inclusion metadata, j2k_image.c for code-block extraction, and Annex J.1/J.11 for
 * reference decoder flowcharts and code-block decoding examples.
 */

#include "j2k/j2k_ebcot.h"
#include "j2k/j2k_debug.h"

#include <stdlib.h>
#include <string.h>

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.1-D.3, code-blocks are coded as bit-plane coding passes. */
void j2k_codeblock_stream_init(j2k_codeblock_stream *stream)
{
    j2k_DEBUG_ENTER();
    if (stream == NULL)
        return;
    j2k_mq_stream_init(&stream->mq);
    stream->pass_lengths = NULL;
    stream->pass_decision_counts = NULL;
    stream->pass_distortion_reductions = NULL;
    stream->pass_rd_slopes = NULL;
    stream->zero_bitplanes = 0u;
    stream->coding_passes = 0u;
    stream->magnitude_bitplanes = 0u;
    stream->width = 0u;
    stream->height = 0u;
    stream->subband_orientation = j2k_SUBBAND_LL_LH;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.7, code-block contributions are byte streams referenced from packet headers. */
void j2k_codeblock_stream_free(j2k_codeblock_stream *stream)
{
    j2k_DEBUG_ENTER();
    if (stream == NULL)
        return;
    j2k_mq_stream_free(&stream->mq);
    free(stream->pass_lengths);
    free(stream->pass_decision_counts);
    free(stream->pass_distortion_reductions);
    free(stream->pass_rd_slopes);
    j2k_codeblock_stream_init(stream);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.2, magnitude bit-planes and sign decisions are the inputs to code-block coding passes. */
static uint32_t j2k_abs_i32(int32_t value)
{
    j2k_DEBUG_ENTER();
    return value < 0 ? (uint32_t)(-(value + 1)) + 1u : (uint32_t)value;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.2.1, the most significant non-zero bit-plane determines leading zero bit-planes. */
static uint32_t j2k_required_bitplanes(const int32_t *coefficients, size_t coefficient_count)
{
    j2k_DEBUG_ENTER();
    uint32_t max_magnitude = 0u;
    uint32_t bitplanes = 0u;
    size_t index;

    for (index = 0u; index < coefficient_count; ++index)
    {
        uint32_t magnitude = j2k_abs_i32(coefficients[index]);

        if (magnitude > max_magnitude)
            max_magnitude = magnitude;
    }

    while (max_magnitude != 0u)
    {
        ++bitplanes;
        max_magnitude >>= 1u;
    }

    return bitplanes;
}

enum
{
    j2k_EBCOT_CX_RUN_LENGTH = 17,
    j2k_EBCOT_CX_UNIFORM = 18,
    j2k_EBCOT_CONTEXT_COUNT = 19
};

typedef struct j2k_ebcot_state
{
    uint8_t significant;
    uint8_t negative;
    uint8_t refined;
    uint8_t coded_sigprop;
    uint8_t just_significant;
} j2k_ebcot_state;

typedef struct j2k_ebcot_symbols
{
    uint8_t *contexts;
    uint8_t *decisions;
    size_t count;
    size_t capacity;
} j2k_ebcot_symbols;

static dic_status j2k_ebcot_decode_codeblock_rect_prefix(
    const j2k_codeblock_stream *stream,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    uint32_t pass_limit,
    int32_t *coefficients
);

static dic_status j2k_ebcot_measure_pass_rd(
    const int32_t *coefficients,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    j2k_codeblock_stream *stream
);

static void j2k_ebcot_symbols_init(j2k_ebcot_symbols *symbols)
{
    j2k_DEBUG_ENTER();
    symbols->contexts = NULL;
    symbols->decisions = NULL;
    symbols->count = 0u;
    symbols->capacity = 0u;
}

static void j2k_ebcot_symbols_free(j2k_ebcot_symbols *symbols)
{
    j2k_DEBUG_ENTER();
    free(symbols->contexts);
    free(symbols->decisions);
    j2k_ebcot_symbols_init(symbols);
}

static void j2k_ebcot_symbols_clear(j2k_ebcot_symbols *symbols)
{
    j2k_DEBUG_ENTER();
    if (symbols != NULL)
        symbols->count = 0u;
}

static dic_status j2k_ebcot_symbols_push(
    j2k_ebcot_symbols *symbols,
    uint8_t context,
    uint8_t decision
)
{
    j2k_DEBUG_ENTER();
    uint8_t *new_contexts;
    uint8_t *new_decisions;
    size_t new_capacity;

    if (symbols->count == symbols->capacity)
    {
        new_capacity = symbols->capacity == 0u ? 128u : symbols->capacity * 2u;
        if (new_capacity < symbols->capacity)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_contexts = (uint8_t *)realloc(symbols->contexts, new_capacity * sizeof(symbols->contexts[0]));
        if (new_contexts == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        symbols->contexts = new_contexts;
        new_decisions = (uint8_t *)realloc(symbols->decisions, new_capacity * sizeof(symbols->decisions[0]));
        if (new_decisions == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        symbols->decisions = new_decisions;
        symbols->capacity = new_capacity;
    }

    symbols->contexts[symbols->count] = context;
    symbols->decisions[symbols->count] = (uint8_t)(decision & 1u);
    ++symbols->count;
    return DIC_STATUS_OK;
}

static dic_status j2k_ebcot_symbols_append(
    j2k_ebcot_symbols *destination,
    const j2k_ebcot_symbols *source
)
{
    j2k_DEBUG_ENTER();
    size_t index;

    if (destination == NULL || source == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    for (index = 0u; index < source->count; ++index)
    {
        dic_status status = j2k_ebcot_symbols_push(
            destination,
            source->contexts[index],
            source->decisions[index]
        );
        if (status != DIC_STATUS_OK)
            return status;
    }
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.1 Figure D.1, coefficients are scanned in four-row vertical stripes. */
static size_t j2k_ebcot_index(uint32_t x, uint32_t y, uint32_t width)
{
    j2k_DEBUG_ENTER();
    return (size_t)y * width + x;
}

static int j2k_ebcot_in_bounds(int x, int y, uint32_t width, uint32_t height)
{
    j2k_DEBUG_ENTER();
    return x >= 0 && y >= 0 && (uint32_t)x < width && (uint32_t)y < height;
}

static uint8_t j2k_ebcot_sig_at(
    const j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    int x,
    int y
)
{
    j2k_DEBUG_ENTER();
    if (!j2k_ebcot_in_bounds(x, y, width, height))
        return 0u;
    return state[j2k_ebcot_index((uint32_t)x, (uint32_t)y, width)].significant;
}

static int j2k_ebcot_sign_at(
    const j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    int x,
    int y
)
{
    j2k_DEBUG_ENTER();
    if (!j2k_ebcot_in_bounds(x, y, width, height))
        return 0;
    if (!state[j2k_ebcot_index((uint32_t)x, (uint32_t)y, width)].significant)
        return 0;
    return state[j2k_ebcot_index((uint32_t)x, (uint32_t)y, width)].negative ? -1 : 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.1 Table D.1, significance contexts depend on horizontal, vertical, and diagonal significant neighbors. */
static uint8_t j2k_ebcot_significance_context(
    const j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y,
    j2k_subband_orientation orientation
)
{
    j2k_DEBUG_ENTER();
    unsigned int h = j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y)
        + j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y);
    unsigned int v = j2k_ebcot_sig_at(state, width, height, (int)x, (int)y - 1)
        + j2k_ebcot_sig_at(state, width, height, (int)x, (int)y + 1);
    unsigned int d = j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y - 1)
        + j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y - 1)
        + j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y + 1)
        + j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y + 1);

    if (orientation == j2k_SUBBAND_HL)
    {
        unsigned int tmp = h;
        h = v;
        v = tmp;
    }

    if (orientation == j2k_SUBBAND_HH)
    {
        unsigned int hv = h + v;

        if (d >= 3u)
            return 8u;
        if (d == 2u)
            return hv >= 1u ? 7u : 6u;
        if (d == 1u)
            return hv >= 2u ? 5u : (hv == 1u ? 4u : 3u);
        return hv >= 2u ? 2u : (hv == 1u ? 1u : 0u);
    }

    if (h == 2u)
        return 8u;
    if (h == 1u)
    {
        if (v >= 1u)
            return 7u;
        return d >= 1u ? 6u : 5u;
    }
    if (v == 2u)
        return 4u;
    if (v == 1u)
        return 3u;
    if (d >= 2u)
        return 2u;
    return d == 1u ? 1u : 0u;
}

static int j2k_ebcot_pair_contribution(int first, int second)
{
    j2k_DEBUG_ENTER();
    if (first == second)
        return first;
    if (first == 0)
        return second;
    if (second == 0)
        return first;
    return 0;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.2 Tables D.2-D.3 and Equation D-1, sign coding uses neighbor sign contributions and XORbit. */
static uint8_t j2k_ebcot_sign_context(
    const j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y,
    uint8_t *xor_bit
)
{
    j2k_DEBUG_ENTER();
    int h = j2k_ebcot_pair_contribution(
        j2k_ebcot_sign_at(state, width, height, (int)x - 1, (int)y),
        j2k_ebcot_sign_at(state, width, height, (int)x + 1, (int)y)
    );
    int v = j2k_ebcot_pair_contribution(
        j2k_ebcot_sign_at(state, width, height, (int)x, (int)y - 1),
        j2k_ebcot_sign_at(state, width, height, (int)x, (int)y + 1)
    );

    *xor_bit = 0u;
    if (h < 0)
    {
        h = -h;
        v = -v;
        *xor_bit = 1u;
    }

    if (h == 1 && v == 1)
        return 13u;
    if (h == 1 && v == 0)
        return 12u;
    if (h == 1 && v == -1)
        return 11u;
    if (h == 0 && v == 1)
        return 10u;
    if (h == 0 && v == 0)
        return 9u;
    if (h == 0 && v == -1)
    {
        *xor_bit = 1u;
        return 10u;
    }
    return 9u;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.3 Table D.4, magnitude refinement has three contexts. */
static uint8_t j2k_ebcot_magnitude_context(
    const j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y
)
{
    j2k_DEBUG_ENTER();
    size_t index = j2k_ebcot_index(x, y, width);
    unsigned int neighbors;

    if (state[index].refined)
        return 16u;

    neighbors = j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y)
        + j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y)
        + j2k_ebcot_sig_at(state, width, height, (int)x, (int)y - 1)
        + j2k_ebcot_sig_at(state, width, height, (int)x, (int)y + 1)
        + j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y - 1)
        + j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y - 1)
        + j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y + 1)
        + j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y + 1);

    return neighbors == 0u ? 14u : 15u;
}

static void j2k_ebcot_initial_contexts(j2k_mq_context_state *contexts)
{
    j2k_DEBUG_ENTER();
    j2k_mq_contexts_init(contexts, j2k_EBCOT_CONTEXT_COUNT);
    contexts[0].index = 4u;
    contexts[j2k_EBCOT_CX_RUN_LENGTH].index = 3u;
    contexts[j2k_EBCOT_CX_UNIFORM].index = 46u;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.7 and Figure C.11, a terminal 0xFF from MQ FLUSH is discarded rather than byte-stuffed into the code-block contribution. */
static dic_status j2k_ebcot_trim_terminal_ff_stream(j2k_mq_stream *stream)
{
    j2k_DEBUG_ENTER();
    if (stream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (stream->byte_count == 0u || stream->data[stream->byte_count - 1u] != 0xffu)
        return DIC_STATUS_OK;

    --stream->byte_count;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.7, pass length tables measure terminated byte segments concatenated into the code-block contribution. */
static dic_status j2k_ebcot_append_mq_segment(
    j2k_mq_stream *destination,
    const j2k_mq_stream *segment
)
{
    j2k_DEBUG_ENTER();
    uint8_t *new_data;

    if (destination == NULL || segment == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (segment->byte_count == 0u)
        return DIC_STATUS_OK;
    if (segment->data == NULL || destination->byte_count > (size_t)-1 - segment->byte_count)
        return DIC_STATUS_INVALID_ARGUMENT;

    new_data = (uint8_t *)realloc(destination->data, destination->byte_count + segment->byte_count);
    if (new_data == NULL)
        return DIC_STATUS_MEMORY_ERROR;
    destination->data = new_data;
    memcpy(destination->data + destination->byte_count, segment->data, segment->byte_count);
    destination->byte_count += segment->byte_count;
    destination->bit_count += segment->bit_count;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, D.3.1-D.3.4 and A.6.1 code-block style bit 2, this path terminates each coding pass and records the segment length. */
static dic_status j2k_ebcot_encode_terminated_pass(
    j2k_codeblock_stream *stream,
    uint32_t pass_index,
    j2k_mq_context_state *contexts,
    const j2k_ebcot_symbols *symbols
)
{
    j2k_DEBUG_ENTER();
    j2k_mq_stream segment;
    dic_status status = DIC_STATUS_OK;

    if (stream == NULL || contexts == NULL || symbols == NULL || pass_index >= stream->coding_passes)
        return DIC_STATUS_INVALID_ARGUMENT;

    j2k_mq_stream_init(&segment);
    if (symbols->count > 0u)
    {
        status = j2k_mq_encode_decisions_with_state_result(
            contexts,
            j2k_EBCOT_CONTEXT_COUNT,
            symbols->contexts,
            symbols->decisions,
            symbols->count,
            &segment,
            contexts,
            j2k_EBCOT_CONTEXT_COUNT
        );
        if (status == DIC_STATUS_OK)
            status = j2k_ebcot_trim_terminal_ff_stream(&segment);
        if (status == DIC_STATUS_OK)
            status = j2k_ebcot_append_mq_segment(&stream->mq, &segment);
    }
    if (status == DIC_STATUS_OK)
    {
        stream->pass_lengths[pass_index] = segment.byte_count;
        stream->pass_decision_counts[pass_index] = symbols->count;
    }
    j2k_mq_stream_free(&segment);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.2, a newly significant coefficient is followed immediately by its sign bit. */
static dic_status j2k_ebcot_emit_sign(
    const int32_t *coefficients,
    j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y,
    j2k_ebcot_symbols *symbols
)
{
    j2k_DEBUG_ENTER();
    size_t index = j2k_ebcot_index(x, y, width);
    uint8_t xor_bit = 0u;
    uint8_t context = j2k_ebcot_sign_context(state, width, height, x, y, &xor_bit);
    uint8_t sign_bit = coefficients[index] < 0 ? 1u : 0u;

    state[index].significant = 1u;
    state[index].negative = sign_bit;
    state[index].just_significant = 1u;
    return j2k_ebcot_symbols_push(symbols, context, (uint8_t)(sign_bit ^ xor_bit));
}

static uint8_t j2k_ebcot_bit_of(int32_t value, uint32_t bitplane)
{
    j2k_DEBUG_ENTER();
    return (uint8_t)((j2k_abs_i32(value) >> bitplane) & 1u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.1, significance propagation codes insignificant coefficients with non-zero context. */
static dic_status j2k_ebcot_encode_sigprop_pass(
    const int32_t *coefficients,
    j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    uint32_t bitplane,
    j2k_ebcot_symbols *symbols
)
{
    j2k_DEBUG_ENTER();
    uint32_t stripe_y;

    for (stripe_y = 0u; stripe_y < height; stripe_y += 4u)
    {
        uint32_t x;
        uint32_t stripe_end = stripe_y + 4u < height ? stripe_y + 4u : height;

        for (x = 0u; x < width; ++x)
        {
            uint32_t y;

            for (y = stripe_y; y < stripe_end; ++y)
            {
                size_t index = j2k_ebcot_index(x, y, width);
                uint8_t context;
                uint8_t bit;
                dic_status status;

                state[index].coded_sigprop = 0u;
                if (state[index].significant)
                    continue;
                context = j2k_ebcot_significance_context(state, width, height, x, y, orientation);
                if (context == 0u)
                    continue;
                bit = j2k_ebcot_bit_of(coefficients[index], bitplane);
                status = j2k_ebcot_symbols_push(symbols, context, bit);
                if (status != DIC_STATUS_OK)
                    return status;
                state[index].coded_sigprop = 1u;
                if (bit != 0u)
                {
                    status = j2k_ebcot_emit_sign(coefficients, state, width, height, x, y, symbols);
                    if (status != DIC_STATUS_OK)
                        return status;
                }
            }
        }
    }

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.3, magnitude refinement skips coefficients newly significant in the preceding pass. */
static dic_status j2k_ebcot_encode_magref_pass(
    const int32_t *coefficients,
    j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t bitplane,
    j2k_ebcot_symbols *symbols
)
{
    j2k_DEBUG_ENTER();
    uint32_t stripe_y;

    for (stripe_y = 0u; stripe_y < height; stripe_y += 4u)
    {
        uint32_t x;
        uint32_t stripe_end = stripe_y + 4u < height ? stripe_y + 4u : height;

        for (x = 0u; x < width; ++x)
        {
            uint32_t y;

            for (y = stripe_y; y < stripe_end; ++y)
            {
                size_t index = j2k_ebcot_index(x, y, width);
                dic_status status;

                if (!state[index].significant || state[index].just_significant)
                    continue;
                status = j2k_ebcot_symbols_push(
                    symbols,
                    j2k_ebcot_magnitude_context(state, width, height, x, y),
                    j2k_ebcot_bit_of(coefficients[index], bitplane)
                );
                if (status != DIC_STATUS_OK)
                    return status;
                state[index].refined = 1u;
            }
        }
    }

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.4 and Table D.5, cleanup pass codes remaining insignificant coefficients and run-length groups. */
static dic_status j2k_ebcot_encode_cleanup_pass(
    const int32_t *coefficients,
    j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    uint32_t bitplane,
    j2k_ebcot_symbols *symbols
)
{
    j2k_DEBUG_ENTER();
    uint32_t stripe_y;

    for (stripe_y = 0u; stripe_y < height; stripe_y += 4u)
    {
        uint32_t x;
        uint32_t stripe_end = stripe_y + 4u < height ? stripe_y + 4u : height;

        for (x = 0u; x < width; ++x)
        {
            uint32_t y = stripe_y;

            while (y < stripe_end)
            {
                size_t index = j2k_ebcot_index(x, y, width);
                uint8_t context;
                uint8_t bit;
                dic_status status;

                if (state[index].significant || state[index].coded_sigprop)
                {
                    ++y;
                    continue;
                }

                if (stripe_end - y == 4u)
                {
                    uint32_t row;
                    int run_candidate = 1;
                    int first_nonzero = -1;

                    for (row = 0u; row < 4u; ++row)
                    {
                        size_t row_index = j2k_ebcot_index(x, y + row, width);

                        if (state[row_index].significant
                            || state[row_index].coded_sigprop
                            || j2k_ebcot_significance_context(state, width, height, x, y + row, orientation) != 0u)
                        {
                            run_candidate = 0;
                            break;
                        }
                        if (j2k_ebcot_bit_of(coefficients[row_index], bitplane) != 0u && first_nonzero < 0)
                            first_nonzero = (int)row;
                    }

                    if (run_candidate)
                    {
                        status = j2k_ebcot_symbols_push(
                            symbols,
                            j2k_EBCOT_CX_RUN_LENGTH,
                            first_nonzero >= 0 ? 1u : 0u
                        );
                        if (status != DIC_STATUS_OK)
                            return status;
                        if (first_nonzero < 0)
                        {
                            y += 4u;
                            continue;
                        }
                        status = j2k_ebcot_symbols_push(
                            symbols,
                            j2k_EBCOT_CX_UNIFORM,
                            (uint8_t)(((unsigned int)first_nonzero >> 1u) & 1u)
                        );
                        if (status != DIC_STATUS_OK)
                            return status;
                        status = j2k_ebcot_symbols_push(
                            symbols,
                            j2k_EBCOT_CX_UNIFORM,
                            (uint8_t)((unsigned int)first_nonzero & 1u)
                        );
                        if (status != DIC_STATUS_OK)
                            return status;
                        status = j2k_ebcot_emit_sign(coefficients, state, width, height, x, y + (uint32_t)first_nonzero, symbols);
                        if (status != DIC_STATUS_OK)
                            return status;
                        y += (uint32_t)first_nonzero + 1u;
                        continue;
                    }
                }

                context = j2k_ebcot_significance_context(state, width, height, x, y, orientation);
                bit = j2k_ebcot_bit_of(coefficients[index], bitplane);
                status = j2k_ebcot_symbols_push(symbols, context, bit);
                if (status != DIC_STATUS_OK)
                    return status;
                if (bit != 0u)
                {
                    status = j2k_ebcot_emit_sign(coefficients, state, width, height, x, y, symbols);
                    if (status != DIC_STATUS_OK)
                        return status;
                }
                ++y;
            }
        }
    }

    return DIC_STATUS_OK;
}

static uint32_t j2k_ebcot_pass_count_for_bitplanes(uint32_t bitplanes)
{
    j2k_DEBUG_ENTER();
    if (bitplanes == 0u)
        return 1u;
    return 1u + 3u * (bitplanes - 1u);
}

static void j2k_ebcot_clear_pass_flags(j2k_ebcot_state *state, size_t coefficient_count)
{
    j2k_DEBUG_ENTER();
    size_t index;

    for (index = 0u; index < coefficient_count; ++index)
    {
        state[index].coded_sigprop = 0u;
        state[index].just_significant = 0u;
    }
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3 and Figure D.3, the first significant bit-plane has cleanup only; lower bit-planes then emit significance propagation, magnitude refinement, and cleanup passes. */
dic_status j2k_ebcot_encode_codeblock_rect_aggregate(
    const int32_t *coefficients,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    j2k_codeblock_stream *stream
)
{
    j2k_DEBUG_ENTER();
    j2k_ebcot_state *state = NULL;
    j2k_ebcot_symbols symbols;
    j2k_ebcot_symbols all_symbols;
    j2k_mq_context_state initial_contexts[j2k_EBCOT_CONTEXT_COUNT];
    size_t coefficient_count;
    uint32_t bitplanes;
    uint32_t plane;
    uint32_t pass_index = 0u;
    dic_status status = DIC_STATUS_OK;

    j2k_ebcot_symbols_init(&symbols);
    j2k_ebcot_symbols_init(&all_symbols);
    if (coefficients == NULL || stream == NULL || width == 0u || height == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (orientation > j2k_SUBBAND_HH)
        return DIC_STATUS_INVALID_ARGUMENT;
    if ((size_t)width > (size_t)-1 / height)
        return DIC_STATUS_INVALID_ARGUMENT;

    coefficient_count = (size_t)width * height;
    state = (j2k_ebcot_state *)calloc(coefficient_count, sizeof(state[0]));
    if (state == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    j2k_codeblock_stream_free(stream);
    bitplanes = j2k_required_bitplanes(coefficients, coefficient_count);
    stream->coding_passes = bitplanes == 0u ? 0u : j2k_ebcot_pass_count_for_bitplanes(bitplanes);
    if (stream->coding_passes > 0u)
    {
        stream->pass_decision_counts = (size_t *)calloc(stream->coding_passes, sizeof(stream->pass_decision_counts[0]));
        if (stream->pass_decision_counts == NULL)
        {
            status = DIC_STATUS_MEMORY_ERROR;
        }
    }

    if (status == DIC_STATUS_OK && bitplanes != 0u)
    {
        j2k_ebcot_initial_contexts(initial_contexts);
        for (plane = bitplanes; plane > 0u && status == DIC_STATUS_OK; --plane)
        {
            uint32_t bitplane = plane - 1u;

            if (plane != bitplanes)
            {
                j2k_ebcot_symbols_clear(&symbols);
                status = j2k_ebcot_encode_sigprop_pass(
                    coefficients,
                    state,
                    width,
                    height,
                    orientation,
                    bitplane,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;
                stream->pass_decision_counts[pass_index++] = symbols.count;
                status = j2k_ebcot_symbols_append(&all_symbols, &symbols);
                if (status != DIC_STATUS_OK)
                    break;
                j2k_ebcot_symbols_clear(&symbols);
                status = j2k_ebcot_encode_magref_pass(
                    coefficients,
                    state,
                    width,
                    height,
                    bitplane,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;
                stream->pass_decision_counts[pass_index++] = symbols.count;
                status = j2k_ebcot_symbols_append(&all_symbols, &symbols);
                if (status != DIC_STATUS_OK)
                    break;
            }
            j2k_ebcot_symbols_clear(&symbols);
            status = j2k_ebcot_encode_cleanup_pass(
                coefficients,
                state,
                width,
                height,
                orientation,
                bitplane,
                &symbols
            );
            if (status != DIC_STATUS_OK)
                break;
            stream->pass_decision_counts[pass_index++] = symbols.count;
            status = j2k_ebcot_symbols_append(&all_symbols, &symbols);
            if (status == DIC_STATUS_OK)
                j2k_ebcot_clear_pass_flags(state, coefficient_count);
        }
        if (status == DIC_STATUS_OK)
        {
            status = j2k_mq_encode_decisions_with_state_result(
                initial_contexts,
                j2k_EBCOT_CONTEXT_COUNT,
                all_symbols.contexts,
                all_symbols.decisions,
                all_symbols.count,
                &stream->mq,
                NULL,
                0u
            );
        }
        if (status == DIC_STATUS_OK)
            status = j2k_ebcot_trim_terminal_ff_stream(&stream->mq);
    }

    if (status == DIC_STATUS_OK)
    {
        stream->magnitude_bitplanes = bitplanes;
        stream->zero_bitplanes = 0u;
        stream->width = width;
        stream->height = height;
        stream->subband_orientation = (uint8_t)orientation;
    }
    else
    {
        j2k_codeblock_stream_free(stream);
    }

    j2k_ebcot_symbols_free(&all_symbols);
    j2k_ebcot_symbols_free(&symbols);
    free(state);
    return status;
}

dic_status j2k_ebcot_encode_codeblock_rect(
    const int32_t *coefficients,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    j2k_codeblock_stream *stream
)
{
    j2k_DEBUG_ENTER();
    j2k_ebcot_state *state = NULL;
    j2k_ebcot_symbols symbols;
    j2k_mq_context_state initial_contexts[j2k_EBCOT_CONTEXT_COUNT];
    size_t coefficient_count;
    uint32_t bitplanes;
    uint32_t plane;
    uint32_t pass_index = 0u;
    dic_status status = DIC_STATUS_OK;

    j2k_ebcot_symbols_init(&symbols);
    if (coefficients == NULL || stream == NULL || width == 0u || height == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (orientation > j2k_SUBBAND_HH)
        return DIC_STATUS_INVALID_ARGUMENT;
    if ((size_t)width > (size_t)-1 / height)
        return DIC_STATUS_INVALID_ARGUMENT;

    coefficient_count = (size_t)width * height;
    state = (j2k_ebcot_state *)calloc(coefficient_count, sizeof(state[0]));
    if (state == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    j2k_codeblock_stream_free(stream);
    bitplanes = j2k_required_bitplanes(coefficients, coefficient_count);
    stream->coding_passes = bitplanes == 0u ? 0u : j2k_ebcot_pass_count_for_bitplanes(bitplanes);
    if (stream->coding_passes > 0u)
    {
        stream->pass_lengths = (size_t *)calloc(stream->coding_passes, sizeof(stream->pass_lengths[0]));
        stream->pass_decision_counts = (size_t *)calloc(stream->coding_passes, sizeof(stream->pass_decision_counts[0]));
        stream->pass_distortion_reductions = (double *)calloc(stream->coding_passes, sizeof(stream->pass_distortion_reductions[0]));
        stream->pass_rd_slopes = (double *)calloc(stream->coding_passes, sizeof(stream->pass_rd_slopes[0]));
        if (stream->pass_lengths == NULL
            || stream->pass_decision_counts == NULL
            || stream->pass_distortion_reductions == NULL
            || stream->pass_rd_slopes == NULL)
        {
            status = DIC_STATUS_MEMORY_ERROR;
        }
    }

    if (status == DIC_STATUS_OK && bitplanes != 0u)
    {
        j2k_ebcot_initial_contexts(initial_contexts);
        for (plane = bitplanes; plane > 0u && status == DIC_STATUS_OK; --plane)
        {
            uint32_t bitplane = plane - 1u;

            if (plane != bitplanes)
            {
                j2k_ebcot_symbols_clear(&symbols);
                status = j2k_ebcot_encode_sigprop_pass(
                    coefficients,
                    state,
                    width,
                    height,
                    orientation,
                    bitplane,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;
                status = j2k_ebcot_encode_terminated_pass(
                    stream,
                    pass_index++,
                    initial_contexts,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;
                j2k_ebcot_symbols_clear(&symbols);
                status = j2k_ebcot_encode_magref_pass(
                    coefficients,
                    state,
                    width,
                    height,
                    bitplane,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;
                status = j2k_ebcot_encode_terminated_pass(
                    stream,
                    pass_index++,
                    initial_contexts,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;
            }
            j2k_ebcot_symbols_clear(&symbols);
            status = j2k_ebcot_encode_cleanup_pass(
                coefficients,
                state,
                width,
                height,
                orientation,
                bitplane,
                &symbols
            );
            if (status != DIC_STATUS_OK)
                break;
            status = j2k_ebcot_encode_terminated_pass(
                stream,
                pass_index++,
                initial_contexts,
                &symbols
            );
            if (status == DIC_STATUS_OK)
                j2k_ebcot_clear_pass_flags(state, coefficient_count);
        }
    }

    if (status == DIC_STATUS_OK)
    {
        stream->magnitude_bitplanes = bitplanes;
        stream->zero_bitplanes = 0u;
        stream->width = width;
        stream->height = height;
        stream->subband_orientation = (uint8_t)orientation;
        status = j2k_ebcot_measure_pass_rd(
            coefficients,
            width,
            height,
            orientation,
            stream
        );
    }
    else
    {
        j2k_codeblock_stream_free(stream);
    }

    j2k_ebcot_symbols_free(&symbols);
    free(state);
    return status;
}

dic_status j2k_ebcot_encode_codeblock(
    const int32_t *coefficients,
    size_t coefficient_count,
    j2k_codeblock_stream *stream
)
{
    j2k_DEBUG_ENTER();
    if (coefficient_count > UINT32_MAX)
        return DIC_STATUS_INVALID_ARGUMENT;
    return j2k_ebcot_encode_codeblock_rect(
        coefficients,
        (uint32_t)coefficient_count,
        1u,
        j2k_SUBBAND_LL_LH,
        stream
    );
}

typedef struct j2k_ebcot_decoder_symbols
{
    j2k_mq_decoder_session *session;
    dic_status status;
} j2k_ebcot_decoder_symbols;

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D coding passes may be terminated individually; each pass then decodes from its own byte segment. */
static dic_status j2k_ebcot_decoder_begin_pass(
    const j2k_codeblock_stream *stream,
    uint32_t pass_index,
    size_t *byte_offset,
    j2k_mq_context_state *contexts,
    j2k_mq_decoder_session *session,
    j2k_ebcot_decoder_symbols *symbols
)
{
    j2k_DEBUG_ENTER();
    j2k_mq_stream segment;

    if (stream == NULL || byte_offset == NULL || contexts == NULL || session == NULL || symbols == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (stream->pass_lengths == NULL || pass_index >= stream->coding_passes)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (stream->mq.byte_count > 0u && stream->mq.data == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (*byte_offset > stream->mq.byte_count || stream->pass_lengths[pass_index] > stream->mq.byte_count - *byte_offset)
        return DIC_J2K_MALFORMED_ARITHMETIC_STREAM;

    segment.data = stream->mq.data == NULL ? NULL : (uint8_t *)(stream->mq.data + *byte_offset);
    segment.byte_count = stream->pass_lengths[pass_index];
    segment.bit_count = stream->pass_decision_counts == NULL
        ? (size_t)-1
        : stream->pass_decision_counts[pass_index];
    symbols->session = session;
    symbols->status = DIC_STATUS_OK;
    return j2k_mq_decoder_session_init(
        session,
        &segment,
        contexts,
        j2k_EBCOT_CONTEXT_COUNT
    );
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex C context states persist across terminated passes unless the reset coding style is used. */
static dic_status j2k_ebcot_decoder_finish_pass(
    const j2k_codeblock_stream *stream,
    uint32_t pass_index,
    size_t *byte_offset,
    j2k_mq_context_state *contexts,
    const j2k_mq_decoder_session *session,
    const j2k_ebcot_decoder_symbols *symbols
)
{
    j2k_DEBUG_ENTER();
    size_t index;

    if (stream == NULL || byte_offset == NULL || contexts == NULL || session == NULL || symbols == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (symbols->status != DIC_STATUS_OK)
        return symbols->status;
    if (pass_index >= stream->coding_passes || stream->pass_lengths == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (stream->pass_decision_counts != NULL
        && session->decisions_decoded != stream->pass_decision_counts[pass_index])
    {
        return DIC_J2K_MALFORMED_ARITHMETIC_STREAM;
    }

    for (index = 0u; index < j2k_EBCOT_CONTEXT_COUNT; ++index)
        contexts[index] = session->states[index];
    *byte_offset += stream->pass_lengths[pass_index];
    return DIC_STATUS_OK;
}

static uint8_t j2k_ebcot_take_decision(j2k_ebcot_decoder_symbols *symbols, uint8_t context)
{
    j2k_DEBUG_ENTER();
    uint8_t decision = 0u;

    if (symbols->status != DIC_STATUS_OK)
        return 0u;
    symbols->status = j2k_mq_decoder_session_decode(symbols->session, context, &decision);
    return decision;
}

static void j2k_ebcot_decode_sign(
    j2k_ebcot_decoder_symbols *symbols,
    j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y
)
{
    j2k_DEBUG_ENTER();
    size_t index = j2k_ebcot_index(x, y, width);
    uint8_t xor_bit = 0u;
    uint8_t context;

    context = j2k_ebcot_sign_context(state, width, height, x, y, &xor_bit);
    state[index].significant = 1u;
    state[index].negative = (uint8_t)(j2k_ebcot_take_decision(symbols, context) ^ xor_bit);
    state[index].just_significant = 1u;
}

static void j2k_ebcot_set_magnitude_bit(int32_t *coefficient, uint32_t bitplane)
{
    j2k_DEBUG_ENTER();
    uint32_t magnitude = j2k_abs_i32(*coefficient);

    magnitude |= 1u << bitplane;
    *coefficient = *coefficient < 0 ? -(int32_t)magnitude : (int32_t)magnitude;
}

static void j2k_ebcot_apply_sign(int32_t *coefficient, uint8_t negative)
{
    j2k_DEBUG_ENTER();
    if (negative && *coefficient > 0)
        *coefficient = -*coefficient;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.1, decoder mirrors significance propagation state updates immediately. */
static void j2k_ebcot_decode_sigprop_pass(
    j2k_ebcot_decoder_symbols *symbols,
    int32_t *coefficients,
    j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    uint32_t bitplane
)
{
    j2k_DEBUG_ENTER();
    uint32_t stripe_y;

    for (stripe_y = 0u; stripe_y < height; stripe_y += 4u)
    {
        uint32_t x;
        uint32_t stripe_end = stripe_y + 4u < height ? stripe_y + 4u : height;

        for (x = 0u; x < width; ++x)
        {
            uint32_t y;

            for (y = stripe_y; y < stripe_end; ++y)
            {
                size_t index = j2k_ebcot_index(x, y, width);
                uint8_t context;
                uint8_t bit;

                state[index].coded_sigprop = 0u;
                if (state[index].significant)
                    continue;
                context = j2k_ebcot_significance_context(state, width, height, x, y, orientation);
                if (context == 0u)
                    continue;
                bit = j2k_ebcot_take_decision(symbols, context);
                state[index].coded_sigprop = 1u;
                if (bit != 0u)
                {
                    j2k_ebcot_set_magnitude_bit(coefficients + index, bitplane);
                    j2k_ebcot_decode_sign(symbols, state, width, height, x, y);
                    j2k_ebcot_apply_sign(coefficients + index, state[index].negative);
                }
            }
        }
    }
}

static void j2k_ebcot_decode_magref_pass(
    j2k_ebcot_decoder_symbols *symbols,
    int32_t *coefficients,
    j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t bitplane
)
{
    j2k_DEBUG_ENTER();
    uint32_t stripe_y;

    for (stripe_y = 0u; stripe_y < height; stripe_y += 4u)
    {
        uint32_t x;
        uint32_t stripe_end = stripe_y + 4u < height ? stripe_y + 4u : height;

        for (x = 0u; x < width; ++x)
        {
            uint32_t y;

            for (y = stripe_y; y < stripe_end; ++y)
            {
                size_t index = j2k_ebcot_index(x, y, width);
                uint8_t bit;

                if (!state[index].significant || state[index].just_significant)
                    continue;
                bit = j2k_ebcot_take_decision(
                    symbols,
                    j2k_ebcot_magnitude_context(state, width, height, x, y)
                );
                if (bit != 0u)
                    j2k_ebcot_set_magnitude_bit(coefficients + index, bitplane);
                j2k_ebcot_apply_sign(coefficients + index, state[index].negative);
                state[index].refined = 1u;
            }
        }
    }
}

static void j2k_ebcot_decode_cleanup_pass(
    j2k_ebcot_decoder_symbols *symbols,
    int32_t *coefficients,
    j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    uint32_t bitplane
)
{
    j2k_DEBUG_ENTER();
    uint32_t stripe_y;

    for (stripe_y = 0u; stripe_y < height; stripe_y += 4u)
    {
        uint32_t x;
        uint32_t stripe_end = stripe_y + 4u < height ? stripe_y + 4u : height;

        for (x = 0u; x < width; ++x)
        {
            uint32_t y = stripe_y;

            while (y < stripe_end)
            {
                size_t index = j2k_ebcot_index(x, y, width);
                uint8_t context;
                uint8_t bit;

                if (state[index].significant || state[index].coded_sigprop)
                {
                    ++y;
                    continue;
                }

                if (stripe_end - y == 4u)
                {
                    uint32_t row;
                    int run_candidate = 1;

                    for (row = 0u; row < 4u; ++row)
                    {
                        size_t row_index = j2k_ebcot_index(x, y + row, width);

                        if (state[row_index].significant
                            || state[row_index].coded_sigprop
                            || j2k_ebcot_significance_context(state, width, height, x, y + row, orientation) != 0u)
                        {
                            run_candidate = 0;
                            break;
                        }
                    }
                    if (run_candidate)
                    {
                        bit = j2k_ebcot_take_decision(symbols, j2k_EBCOT_CX_RUN_LENGTH);
                        if (bit == 0u)
                        {
                            y += 4u;
                            continue;
                        }
                        row = ((uint32_t)j2k_ebcot_take_decision(symbols, j2k_EBCOT_CX_UNIFORM) << 1u)
                            | j2k_ebcot_take_decision(symbols, j2k_EBCOT_CX_UNIFORM);
                        index = j2k_ebcot_index(x, y + row, width);
                        j2k_ebcot_set_magnitude_bit(coefficients + index, bitplane);
                        j2k_ebcot_decode_sign(symbols, state, width, height, x, y + row);
                        j2k_ebcot_apply_sign(coefficients + index, state[index].negative);
                        y += row + 1u;
                        continue;
                    }
                }

                context = j2k_ebcot_significance_context(state, width, height, x, y, orientation);
                bit = j2k_ebcot_take_decision(symbols, context);
                if (bit != 0u)
                {
                    j2k_ebcot_set_magnitude_bit(coefficients + index, bitplane);
                    j2k_ebcot_decode_sign(symbols, state, width, height, x, y);
                    j2k_ebcot_apply_sign(coefficients + index, state[index].negative);
                }
                ++y;
            }
        }
    }
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3 and Annex C.4, decoder reconstructs a prefix of the state-driven pass order for RD truncation tests. */
static dic_status j2k_ebcot_decode_codeblock_rect_prefix(
    const j2k_codeblock_stream *stream,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    uint32_t pass_limit,
    int32_t *coefficients
)
{
    j2k_DEBUG_ENTER();
    j2k_ebcot_state *state = NULL;
    j2k_mq_context_state initial_contexts[j2k_EBCOT_CONTEXT_COUNT];
    j2k_mq_decoder_session session;
    j2k_ebcot_decoder_symbols symbols;
    size_t coefficient_count;
    size_t byte_offset = 0u;
    uint32_t plane;
    uint32_t pass_index = 0u;
    dic_status status;

    if (stream == NULL || coefficients == NULL || width == 0u || height == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (pass_limit > stream->coding_passes)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (orientation > j2k_SUBBAND_HH)
        return DIC_STATUS_INVALID_ARGUMENT;
    if ((size_t)width > (size_t)-1 / height)
        return DIC_STATUS_INVALID_ARGUMENT;

    coefficient_count = (size_t)width * height;
    state = (j2k_ebcot_state *)calloc(coefficient_count, sizeof(state[0]));
    if (state == NULL)
    {
        free(state);
        return DIC_STATUS_MEMORY_ERROR;
    }

    memset(coefficients, 0, coefficient_count * sizeof(coefficients[0]));
    j2k_ebcot_initial_contexts(initial_contexts);

    if (stream->magnitude_bitplanes != 0u && pass_limit != 0u && stream->pass_lengths != NULL)
    {
        status = DIC_STATUS_OK;
        for (plane = stream->magnitude_bitplanes; plane > 0u && status == DIC_STATUS_OK; --plane)
        {
            uint32_t bitplane = plane - 1u;

            if (plane != stream->magnitude_bitplanes)
            {
                if (pass_index >= pass_limit)
                    break;
                status = j2k_ebcot_decoder_begin_pass(
                    stream,
                    pass_index,
                    &byte_offset,
                    initial_contexts,
                    &session,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;
                j2k_ebcot_decode_sigprop_pass(
                    &symbols,
                    coefficients,
                    state,
                    width,
                    height,
                    orientation,
                    bitplane
                );
                status = j2k_ebcot_decoder_finish_pass(
                    stream,
                    pass_index++,
                    &byte_offset,
                    initial_contexts,
                    &session,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;

                if (pass_index >= pass_limit)
                    break;
                status = j2k_ebcot_decoder_begin_pass(
                    stream,
                    pass_index,
                    &byte_offset,
                    initial_contexts,
                    &session,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;
                j2k_ebcot_decode_magref_pass(
                    &symbols,
                    coefficients,
                    state,
                    width,
                    height,
                    bitplane
                );
                status = j2k_ebcot_decoder_finish_pass(
                    stream,
                    pass_index++,
                    &byte_offset,
                    initial_contexts,
                    &session,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;
            }

            if (pass_index >= pass_limit)
                break;
            status = j2k_ebcot_decoder_begin_pass(
                stream,
                pass_index,
                &byte_offset,
                initial_contexts,
                &session,
                &symbols
            );
            if (status != DIC_STATUS_OK)
                break;
            j2k_ebcot_decode_cleanup_pass(
                &symbols,
                coefficients,
                state,
                width,
                height,
                orientation,
                bitplane
            );
            status = j2k_ebcot_decoder_finish_pass(
                stream,
                pass_index++,
                &byte_offset,
                initial_contexts,
                &session,
                &symbols
            );
            if (status == DIC_STATUS_OK)
                j2k_ebcot_clear_pass_flags(state, coefficient_count);
        }
        if (status == DIC_STATUS_OK && pass_index != pass_limit)
            status = DIC_J2K_MALFORMED_ARITHMETIC_STREAM;
        if (status == DIC_STATUS_OK && pass_limit == stream->coding_passes && byte_offset != stream->mq.byte_count)
            status = DIC_J2K_MALFORMED_ARITHMETIC_STREAM;
    }
    else
    {
        if (pass_limit != stream->coding_passes)
        {
            free(state);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
        status = j2k_mq_decoder_session_init(
            &session,
            &stream->mq,
            initial_contexts,
            j2k_EBCOT_CONTEXT_COUNT
        );
        if (status == DIC_STATUS_OK)
        {
            symbols.session = &session;
            symbols.status = DIC_STATUS_OK;

            if (stream->magnitude_bitplanes != 0u && stream->coding_passes != 0u)
            {
                for (plane = stream->magnitude_bitplanes; plane > 0u; --plane)
                {
                    uint32_t bitplane = plane - 1u;

                    if (plane != stream->magnitude_bitplanes)
                    {
                        j2k_ebcot_decode_sigprop_pass(
                            &symbols,
                            coefficients,
                            state,
                            width,
                            height,
                            orientation,
                            bitplane
                        );
                        j2k_ebcot_decode_magref_pass(
                            &symbols,
                            coefficients,
                            state,
                            width,
                            height,
                            bitplane
                        );
                    }
                    j2k_ebcot_decode_cleanup_pass(
                        &symbols,
                        coefficients,
                        state,
                        width,
                        height,
                        orientation,
                        bitplane
                    );
                    j2k_ebcot_clear_pass_flags(state, coefficient_count);
                }
            }
            status = symbols.status;
            if (status == DIC_STATUS_OK
                && stream->mq.bit_count != (size_t)-1
                && session.decisions_decoded != stream->mq.bit_count)
                status = DIC_J2K_MALFORMED_ARITHMETIC_STREAM;
        }
    }

    free(state);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3 and Annex C.4, public decoding consumes the full coded pass sequence. */
dic_status j2k_ebcot_decode_codeblock_rect(
    const j2k_codeblock_stream *stream,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    int32_t *coefficients
)
{
    j2k_DEBUG_ENTER();
    if (stream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    return j2k_ebcot_decode_codeblock_rect_prefix(
        stream,
        width,
        height,
        orientation,
        stream->coding_passes,
        coefficients
    );
}

static double j2k_ebcot_sse(
    const int32_t *reference,
    const int32_t *candidate,
    size_t coefficient_count
)
{
    j2k_DEBUG_ENTER();
    size_t index;
    double distortion = 0.0;

    for (index = 0u; index < coefficient_count; ++index)
    {
        double difference = (double)reference[index] - (double)candidate[index];
        distortion += difference * difference;
    }
    return distortion;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.8, layer formation uses truncation points selected by rate-distortion slope. */
static dic_status j2k_ebcot_measure_pass_rd(
    const int32_t *coefficients,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    j2k_codeblock_stream *stream
)
{
    j2k_DEBUG_ENTER();
    int32_t *decoded = NULL;
    size_t coefficient_count;
    double previous_distortion;
    uint32_t pass;
    dic_status status = DIC_STATUS_OK;

    if (coefficients == NULL || stream == NULL || width == 0u || height == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (stream->coding_passes == 0u)
        return DIC_STATUS_OK;
    if (stream->pass_distortion_reductions == NULL || stream->pass_rd_slopes == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if ((size_t)width > (size_t)-1 / height)
        return DIC_STATUS_INVALID_ARGUMENT;

    coefficient_count = (size_t)width * (size_t)height;
    decoded = (int32_t *)calloc(coefficient_count, sizeof(decoded[0]));
    if (decoded == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    previous_distortion = j2k_ebcot_sse(coefficients, decoded, coefficient_count);
    for (pass = 1u; pass <= stream->coding_passes; ++pass)
    {
        double distortion;
        double reduction;

        status = j2k_ebcot_decode_codeblock_rect_prefix(
            stream,
            width,
            height,
            orientation,
            pass,
            decoded
        );
        if (status != DIC_STATUS_OK)
            break;
        distortion = j2k_ebcot_sse(coefficients, decoded, coefficient_count);
        reduction = previous_distortion > distortion ? previous_distortion - distortion : 0.0;
        stream->pass_distortion_reductions[pass - 1u] = reduction;
        stream->pass_rd_slopes[pass - 1u] = stream->pass_lengths[pass - 1u] == 0u
            ? reduction
            : reduction / (double)stream->pass_lengths[pass - 1u];
        previous_distortion = distortion;
    }

    free(decoded);
    return status;
}

dic_status j2k_ebcot_decode_codeblock(
    const j2k_codeblock_stream *stream,
    size_t coefficient_count,
    int32_t *coefficients
)
{
    j2k_DEBUG_ENTER();
    if (coefficient_count > UINT32_MAX)
        return DIC_STATUS_INVALID_ARGUMENT;
    return j2k_ebcot_decode_codeblock_rect(
        stream,
        (uint32_t)coefficient_count,
        1u,
        j2k_SUBBAND_LL_LH,
        coefficients
    );
}

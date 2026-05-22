/**
 * @file dic_j2k_ebcot.c
 * @brief Implements JPEG 2000 EBCOT code-block modelling from T.800 Annex D.
 *
 * The encoder and decoder build significance, refinement, cleanup, sign, run-length, and
 * uniform-context decision streams for one code-block, then pass those decisions through
 * the Annex C MQ coder. The implementation is intentionally compact: it models the core
 * coding-pass behavior used by this project and stores one aggregate code-block stream
 * containing the complete cleanup/significance-propagation/magnitude-refinement pass
 * sequence for all non-zero magnitude bit-planes. Optional termination, bypass, and
 * segmentation styles from Annex D are still fixed to the regular MQ-coded path.
 *
 * References: dic_j2k_mq.c for Annex C MQ coding, dic_j2k_packet.c for Annex B packet
 * inclusion metadata, dic_j2k_image.c for code-block extraction, and Annex J.1/J.11 for
 * reference decoder flowcharts and code-block decoding examples.
 */

#include "j2k/dic_j2k_ebcot.h"
#include "j2k/dic_j2k_debug.h"

#include <stdlib.h>
#include <string.h>

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.1-D.3, code-blocks are coded as bit-plane coding passes. */
void dic_j2k_codeblock_stream_init(dic_j2k_codeblock_stream *stream)
{
    DIC_J2K_DEBUG_ENTER();
    if (stream == NULL)
        return;
    dic_j2k_mq_stream_init(&stream->mq);
    stream->zero_bitplanes = 0u;
    stream->coding_passes = 0u;
    stream->magnitude_bitplanes = 0u;
    stream->width = 0u;
    stream->height = 0u;
    stream->subband_orientation = DIC_J2K_SUBBAND_LL_LH;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.7, code-block contributions are byte streams referenced from packet headers. */
void dic_j2k_codeblock_stream_free(dic_j2k_codeblock_stream *stream)
{
    DIC_J2K_DEBUG_ENTER();
    if (stream == NULL)
        return;
    dic_j2k_mq_stream_free(&stream->mq);
    dic_j2k_codeblock_stream_init(stream);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.2, magnitude bit-planes and sign decisions are the inputs to code-block coding passes. */
static uint32_t dic_j2k_abs_i32(int32_t value)
{
    DIC_J2K_DEBUG_ENTER();
    return value < 0 ? (uint32_t)(-(value + 1)) + 1u : (uint32_t)value;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.2.1, the most significant non-zero bit-plane determines leading zero bit-planes. */
static uint32_t dic_j2k_required_bitplanes(const int32_t *coefficients, size_t coefficient_count)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t max_magnitude = 0u;
    uint32_t bitplanes = 0u;
    size_t index;

    for (index = 0u; index < coefficient_count; ++index)
    {
        uint32_t magnitude = dic_j2k_abs_i32(coefficients[index]);

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
    DIC_J2K_EBCOT_CX_RUN_LENGTH = 17,
    DIC_J2K_EBCOT_CX_UNIFORM = 18,
    DIC_J2K_EBCOT_CONTEXT_COUNT = 19
};

typedef struct dic_j2k_ebcot_state
{
    uint8_t significant;
    uint8_t negative;
    uint8_t refined;
    uint8_t coded_sigprop;
    uint8_t just_significant;
} dic_j2k_ebcot_state;

typedef struct dic_j2k_ebcot_symbols
{
    uint8_t *contexts;
    uint8_t *decisions;
    size_t count;
    size_t capacity;
} dic_j2k_ebcot_symbols;

static void dic_j2k_ebcot_symbols_init(dic_j2k_ebcot_symbols *symbols)
{
    DIC_J2K_DEBUG_ENTER();
    symbols->contexts = NULL;
    symbols->decisions = NULL;
    symbols->count = 0u;
    symbols->capacity = 0u;
}

static void dic_j2k_ebcot_symbols_free(dic_j2k_ebcot_symbols *symbols)
{
    DIC_J2K_DEBUG_ENTER();
    free(symbols->contexts);
    free(symbols->decisions);
    dic_j2k_ebcot_symbols_init(symbols);
}

static dic_status dic_j2k_ebcot_symbols_push(
    dic_j2k_ebcot_symbols *symbols,
    uint8_t context,
    uint8_t decision
)
{
    DIC_J2K_DEBUG_ENTER();
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

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.1 Figure D.1, coefficients are scanned in four-row vertical stripes. */
static size_t dic_j2k_ebcot_index(uint32_t x, uint32_t y, uint32_t width)
{
    DIC_J2K_DEBUG_ENTER();
    return (size_t)y * width + x;
}

static int dic_j2k_ebcot_in_bounds(int x, int y, uint32_t width, uint32_t height)
{
    DIC_J2K_DEBUG_ENTER();
    return x >= 0 && y >= 0 && (uint32_t)x < width && (uint32_t)y < height;
}

static uint8_t dic_j2k_ebcot_sig_at(
    const dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    int x,
    int y
)
{
    DIC_J2K_DEBUG_ENTER();
    if (!dic_j2k_ebcot_in_bounds(x, y, width, height))
        return 0u;
    return state[dic_j2k_ebcot_index((uint32_t)x, (uint32_t)y, width)].significant;
}

static int dic_j2k_ebcot_sign_at(
    const dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    int x,
    int y
)
{
    DIC_J2K_DEBUG_ENTER();
    if (!dic_j2k_ebcot_in_bounds(x, y, width, height))
        return 0;
    if (!state[dic_j2k_ebcot_index((uint32_t)x, (uint32_t)y, width)].significant)
        return 0;
    return state[dic_j2k_ebcot_index((uint32_t)x, (uint32_t)y, width)].negative ? -1 : 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.1 Table D.1, significance contexts depend on horizontal, vertical, and diagonal significant neighbors. */
static uint8_t dic_j2k_ebcot_significance_context(
    const dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y,
    dic_j2k_subband_orientation orientation
)
{
    DIC_J2K_DEBUG_ENTER();
    unsigned int h = dic_j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y);
    unsigned int v = dic_j2k_ebcot_sig_at(state, width, height, (int)x, (int)y - 1)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x, (int)y + 1);
    unsigned int d = dic_j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y - 1)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y - 1)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y + 1)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y + 1);

    if (orientation == DIC_J2K_SUBBAND_HL)
    {
        unsigned int tmp = h;
        h = v;
        v = tmp;
    }

    if (orientation == DIC_J2K_SUBBAND_HH)
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

static int dic_j2k_ebcot_pair_contribution(int first, int second)
{
    DIC_J2K_DEBUG_ENTER();
    if (first == second)
        return first;
    if (first == 0)
        return second;
    if (second == 0)
        return first;
    return 0;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.2 Tables D.2-D.3 and Equation D-1, sign coding uses neighbor sign contributions and XORbit. */
static uint8_t dic_j2k_ebcot_sign_context(
    const dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y,
    uint8_t *xor_bit
)
{
    DIC_J2K_DEBUG_ENTER();
    int h = dic_j2k_ebcot_pair_contribution(
        dic_j2k_ebcot_sign_at(state, width, height, (int)x - 1, (int)y),
        dic_j2k_ebcot_sign_at(state, width, height, (int)x + 1, (int)y)
    );
    int v = dic_j2k_ebcot_pair_contribution(
        dic_j2k_ebcot_sign_at(state, width, height, (int)x, (int)y - 1),
        dic_j2k_ebcot_sign_at(state, width, height, (int)x, (int)y + 1)
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
static uint8_t dic_j2k_ebcot_magnitude_context(
    const dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index = dic_j2k_ebcot_index(x, y, width);
    unsigned int neighbors;

    if (state[index].refined)
        return 16u;

    neighbors = dic_j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x, (int)y - 1)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x, (int)y + 1)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y - 1)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y - 1)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x - 1, (int)y + 1)
        + dic_j2k_ebcot_sig_at(state, width, height, (int)x + 1, (int)y + 1);

    return neighbors == 0u ? 14u : 15u;
}

static void dic_j2k_ebcot_initial_contexts(dic_j2k_mq_context_state *contexts)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_mq_contexts_init(contexts, DIC_J2K_EBCOT_CONTEXT_COUNT);
    contexts[0].index = 4u;
    contexts[DIC_J2K_EBCOT_CX_RUN_LENGTH].index = 3u;
    contexts[DIC_J2K_EBCOT_CX_UNIFORM].index = 46u;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.7 and Figure C.11, a terminal 0xFF from MQ FLUSH is discarded rather than byte-stuffed into the code-block contribution. */
static dic_status dic_j2k_ebcot_trim_terminal_ff(dic_j2k_codeblock_stream *stream)
{
    DIC_J2K_DEBUG_ENTER();
    if (stream == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (stream->mq.byte_count == 0u || stream->mq.data[stream->mq.byte_count - 1u] != 0xffu)
        return DIC_STATUS_OK;

    --stream->mq.byte_count;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.2, a newly significant coefficient is followed immediately by its sign bit. */
static dic_status dic_j2k_ebcot_emit_sign(
    const int32_t *coefficients,
    dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y,
    dic_j2k_ebcot_symbols *symbols
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index = dic_j2k_ebcot_index(x, y, width);
    uint8_t xor_bit = 0u;
    uint8_t context = dic_j2k_ebcot_sign_context(state, width, height, x, y, &xor_bit);
    uint8_t sign_bit = coefficients[index] < 0 ? 1u : 0u;

    state[index].significant = 1u;
    state[index].negative = sign_bit;
    state[index].just_significant = 1u;
    return dic_j2k_ebcot_symbols_push(symbols, context, (uint8_t)(sign_bit ^ xor_bit));
}

static uint8_t dic_j2k_ebcot_bit_of(int32_t value, uint32_t bitplane)
{
    DIC_J2K_DEBUG_ENTER();
    return (uint8_t)((dic_j2k_abs_i32(value) >> bitplane) & 1u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.1, significance propagation codes insignificant coefficients with non-zero context. */
static dic_status dic_j2k_ebcot_encode_sigprop_pass(
    const int32_t *coefficients,
    dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    dic_j2k_subband_orientation orientation,
    uint32_t bitplane,
    dic_j2k_ebcot_symbols *symbols
)
{
    DIC_J2K_DEBUG_ENTER();
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
                size_t index = dic_j2k_ebcot_index(x, y, width);
                uint8_t context;
                uint8_t bit;
                dic_status status;

                state[index].coded_sigprop = 0u;
                if (state[index].significant)
                    continue;
                context = dic_j2k_ebcot_significance_context(state, width, height, x, y, orientation);
                if (context == 0u)
                    continue;
                bit = dic_j2k_ebcot_bit_of(coefficients[index], bitplane);
                status = dic_j2k_ebcot_symbols_push(symbols, context, bit);
                if (status != DIC_STATUS_OK)
                    return status;
                state[index].coded_sigprop = 1u;
                if (bit != 0u)
                {
                    status = dic_j2k_ebcot_emit_sign(coefficients, state, width, height, x, y, symbols);
                    if (status != DIC_STATUS_OK)
                        return status;
                }
            }
        }
    }

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.3, magnitude refinement skips coefficients newly significant in the preceding pass. */
static dic_status dic_j2k_ebcot_encode_magref_pass(
    const int32_t *coefficients,
    dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t bitplane,
    dic_j2k_ebcot_symbols *symbols
)
{
    DIC_J2K_DEBUG_ENTER();
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
                size_t index = dic_j2k_ebcot_index(x, y, width);
                dic_status status;

                if (!state[index].significant || state[index].just_significant)
                    continue;
                status = dic_j2k_ebcot_symbols_push(
                    symbols,
                    dic_j2k_ebcot_magnitude_context(state, width, height, x, y),
                    dic_j2k_ebcot_bit_of(coefficients[index], bitplane)
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
static dic_status dic_j2k_ebcot_encode_cleanup_pass(
    const int32_t *coefficients,
    dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    dic_j2k_subband_orientation orientation,
    uint32_t bitplane,
    dic_j2k_ebcot_symbols *symbols
)
{
    DIC_J2K_DEBUG_ENTER();
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
                size_t index = dic_j2k_ebcot_index(x, y, width);
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
                        size_t row_index = dic_j2k_ebcot_index(x, y + row, width);

                        if (state[row_index].significant
                            || state[row_index].coded_sigprop
                            || dic_j2k_ebcot_significance_context(state, width, height, x, y + row, orientation) != 0u)
                        {
                            run_candidate = 0;
                            break;
                        }
                        if (dic_j2k_ebcot_bit_of(coefficients[row_index], bitplane) != 0u && first_nonzero < 0)
                            first_nonzero = (int)row;
                    }

                    if (run_candidate)
                    {
                        status = dic_j2k_ebcot_symbols_push(
                            symbols,
                            DIC_J2K_EBCOT_CX_RUN_LENGTH,
                            first_nonzero >= 0 ? 1u : 0u
                        );
                        if (status != DIC_STATUS_OK)
                            return status;
                        if (first_nonzero < 0)
                        {
                            y += 4u;
                            continue;
                        }
                        status = dic_j2k_ebcot_symbols_push(
                            symbols,
                            DIC_J2K_EBCOT_CX_UNIFORM,
                            (uint8_t)(((unsigned int)first_nonzero >> 1u) & 1u)
                        );
                        if (status != DIC_STATUS_OK)
                            return status;
                        status = dic_j2k_ebcot_symbols_push(
                            symbols,
                            DIC_J2K_EBCOT_CX_UNIFORM,
                            (uint8_t)((unsigned int)first_nonzero & 1u)
                        );
                        if (status != DIC_STATUS_OK)
                            return status;
                        status = dic_j2k_ebcot_emit_sign(coefficients, state, width, height, x, y + (uint32_t)first_nonzero, symbols);
                        if (status != DIC_STATUS_OK)
                            return status;
                        y += (uint32_t)first_nonzero + 1u;
                        continue;
                    }
                }

                context = dic_j2k_ebcot_significance_context(state, width, height, x, y, orientation);
                bit = dic_j2k_ebcot_bit_of(coefficients[index], bitplane);
                status = dic_j2k_ebcot_symbols_push(symbols, context, bit);
                if (status != DIC_STATUS_OK)
                    return status;
                if (bit != 0u)
                {
                    status = dic_j2k_ebcot_emit_sign(coefficients, state, width, height, x, y, symbols);
                    if (status != DIC_STATUS_OK)
                        return status;
                }
                ++y;
            }
        }
    }

    return DIC_STATUS_OK;
}

static uint32_t dic_j2k_ebcot_pass_count_for_bitplanes(uint32_t bitplanes)
{
    DIC_J2K_DEBUG_ENTER();
    if (bitplanes == 0u)
        return 1u;
    return 1u + 3u * (bitplanes - 1u);
}

static void dic_j2k_ebcot_clear_pass_flags(dic_j2k_ebcot_state *state, size_t coefficient_count)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;

    for (index = 0u; index < coefficient_count; ++index)
    {
        state[index].coded_sigprop = 0u;
        state[index].just_significant = 0u;
    }
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3 and Figure D.3, the first significant bit-plane has cleanup only; lower bit-planes then emit significance propagation, magnitude refinement, and cleanup passes. */
dic_status dic_j2k_ebcot_encode_codeblock_rect(
    const int32_t *coefficients,
    uint32_t width,
    uint32_t height,
    dic_j2k_subband_orientation orientation,
    dic_j2k_codeblock_stream *stream
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_ebcot_state *state = NULL;
    dic_j2k_ebcot_symbols symbols;
    dic_j2k_mq_context_state initial_contexts[DIC_J2K_EBCOT_CONTEXT_COUNT];
    size_t coefficient_count;
    uint32_t bitplanes;
    uint32_t plane;
    dic_status status = DIC_STATUS_OK;

    dic_j2k_ebcot_symbols_init(&symbols);
    if (coefficients == NULL || stream == NULL || width == 0u || height == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (orientation > DIC_J2K_SUBBAND_HH)
        return DIC_STATUS_INVALID_ARGUMENT;
    if ((size_t)width > (size_t)-1 / height)
        return DIC_STATUS_INVALID_ARGUMENT;

    coefficient_count = (size_t)width * height;
    state = (dic_j2k_ebcot_state *)calloc(coefficient_count, sizeof(state[0]));
    if (state == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    dic_j2k_codeblock_stream_free(stream);
    bitplanes = dic_j2k_required_bitplanes(coefficients, coefficient_count);

    if (bitplanes != 0u)
    {
        for (plane = bitplanes; plane > 0u && status == DIC_STATUS_OK; --plane)
        {
            uint32_t bitplane = plane - 1u;

            if (plane != bitplanes)
            {
                status = dic_j2k_ebcot_encode_sigprop_pass(
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
                status = dic_j2k_ebcot_encode_magref_pass(
                    coefficients,
                    state,
                    width,
                    height,
                    bitplane,
                    &symbols
                );
                if (status != DIC_STATUS_OK)
                    break;
            }
            status = dic_j2k_ebcot_encode_cleanup_pass(
                coefficients,
                state,
                width,
                height,
                orientation,
                bitplane,
                &symbols
            );
            if (status == DIC_STATUS_OK)
                dic_j2k_ebcot_clear_pass_flags(state, coefficient_count);
        }
    }

    if (status == DIC_STATUS_OK && symbols.count > 0u)
    {
        dic_j2k_ebcot_initial_contexts(initial_contexts);
        status = dic_j2k_mq_encode_decisions_with_states(
            initial_contexts,
            DIC_J2K_EBCOT_CONTEXT_COUNT,
            symbols.contexts,
            symbols.decisions,
            symbols.count,
            &stream->mq
        );
        if (status == DIC_STATUS_OK)
            status = dic_j2k_ebcot_trim_terminal_ff(stream);
    }

    if (status == DIC_STATUS_OK)
    {
        stream->magnitude_bitplanes = bitplanes;
        stream->zero_bitplanes = 0u;
        /* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.6 Table B.4, Npass is the number of coding passes contributed by this packet. */
        stream->coding_passes = bitplanes == 0u ? 0u : dic_j2k_ebcot_pass_count_for_bitplanes(bitplanes);
        stream->width = width;
        stream->height = height;
        stream->subband_orientation = (uint8_t)orientation;
    }

    dic_j2k_ebcot_symbols_free(&symbols);
    free(state);
    return status;
}

dic_status dic_j2k_ebcot_encode_codeblock(
    const int32_t *coefficients,
    size_t coefficient_count,
    dic_j2k_codeblock_stream *stream
)
{
    DIC_J2K_DEBUG_ENTER();
    if (coefficient_count > UINT32_MAX)
        return DIC_STATUS_INVALID_ARGUMENT;
    return dic_j2k_ebcot_encode_codeblock_rect(
        coefficients,
        (uint32_t)coefficient_count,
        1u,
        DIC_J2K_SUBBAND_LL_LH,
        stream
    );
}

typedef struct dic_j2k_ebcot_decoder_symbols
{
    dic_j2k_mq_decoder_session *session;
    dic_status status;
} dic_j2k_ebcot_decoder_symbols;

static uint8_t dic_j2k_ebcot_take_decision(dic_j2k_ebcot_decoder_symbols *symbols, uint8_t context)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t decision = 0u;

    if (symbols->status != DIC_STATUS_OK)
        return 0u;
    symbols->status = dic_j2k_mq_decoder_session_decode(symbols->session, context, &decision);
    return decision;
}

static void dic_j2k_ebcot_decode_sign(
    dic_j2k_ebcot_decoder_symbols *symbols,
    dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index = dic_j2k_ebcot_index(x, y, width);
    uint8_t xor_bit = 0u;
    uint8_t context;

    context = dic_j2k_ebcot_sign_context(state, width, height, x, y, &xor_bit);
    state[index].significant = 1u;
    state[index].negative = (uint8_t)(dic_j2k_ebcot_take_decision(symbols, context) ^ xor_bit);
    state[index].just_significant = 1u;
}

static void dic_j2k_ebcot_set_magnitude_bit(int32_t *coefficient, uint32_t bitplane)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t magnitude = dic_j2k_abs_i32(*coefficient);

    magnitude |= 1u << bitplane;
    *coefficient = *coefficient < 0 ? -(int32_t)magnitude : (int32_t)magnitude;
}

static void dic_j2k_ebcot_apply_sign(int32_t *coefficient, uint8_t negative)
{
    DIC_J2K_DEBUG_ENTER();
    if (negative && *coefficient > 0)
        *coefficient = -*coefficient;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3.1, decoder mirrors significance propagation state updates immediately. */
static void dic_j2k_ebcot_decode_sigprop_pass(
    dic_j2k_ebcot_decoder_symbols *symbols,
    int32_t *coefficients,
    dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    dic_j2k_subband_orientation orientation,
    uint32_t bitplane
)
{
    DIC_J2K_DEBUG_ENTER();
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
                size_t index = dic_j2k_ebcot_index(x, y, width);
                uint8_t context;
                uint8_t bit;

                state[index].coded_sigprop = 0u;
                if (state[index].significant)
                    continue;
                context = dic_j2k_ebcot_significance_context(state, width, height, x, y, orientation);
                if (context == 0u)
                    continue;
                bit = dic_j2k_ebcot_take_decision(symbols, context);
                state[index].coded_sigprop = 1u;
                if (bit != 0u)
                {
                    dic_j2k_ebcot_set_magnitude_bit(coefficients + index, bitplane);
                    dic_j2k_ebcot_decode_sign(symbols, state, width, height, x, y);
                    dic_j2k_ebcot_apply_sign(coefficients + index, state[index].negative);
                }
            }
        }
    }
}

static void dic_j2k_ebcot_decode_magref_pass(
    dic_j2k_ebcot_decoder_symbols *symbols,
    int32_t *coefficients,
    dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    uint32_t bitplane
)
{
    DIC_J2K_DEBUG_ENTER();
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
                size_t index = dic_j2k_ebcot_index(x, y, width);
                uint8_t bit;

                if (!state[index].significant || state[index].just_significant)
                    continue;
                bit = dic_j2k_ebcot_take_decision(
                    symbols,
                    dic_j2k_ebcot_magnitude_context(state, width, height, x, y)
                );
                if (bit != 0u)
                    dic_j2k_ebcot_set_magnitude_bit(coefficients + index, bitplane);
                dic_j2k_ebcot_apply_sign(coefficients + index, state[index].negative);
                state[index].refined = 1u;
            }
        }
    }
}

static void dic_j2k_ebcot_decode_cleanup_pass(
    dic_j2k_ebcot_decoder_symbols *symbols,
    int32_t *coefficients,
    dic_j2k_ebcot_state *state,
    uint32_t width,
    uint32_t height,
    dic_j2k_subband_orientation orientation,
    uint32_t bitplane
)
{
    DIC_J2K_DEBUG_ENTER();
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
                size_t index = dic_j2k_ebcot_index(x, y, width);
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
                        size_t row_index = dic_j2k_ebcot_index(x, y + row, width);

                        if (state[row_index].significant
                            || state[row_index].coded_sigprop
                            || dic_j2k_ebcot_significance_context(state, width, height, x, y + row, orientation) != 0u)
                        {
                            run_candidate = 0;
                            break;
                        }
                    }
                    if (run_candidate)
                    {
                        bit = dic_j2k_ebcot_take_decision(symbols, DIC_J2K_EBCOT_CX_RUN_LENGTH);
                        if (bit == 0u)
                        {
                            y += 4u;
                            continue;
                        }
                        row = ((uint32_t)dic_j2k_ebcot_take_decision(symbols, DIC_J2K_EBCOT_CX_UNIFORM) << 1u)
                            | dic_j2k_ebcot_take_decision(symbols, DIC_J2K_EBCOT_CX_UNIFORM);
                        index = dic_j2k_ebcot_index(x, y + row, width);
                        dic_j2k_ebcot_set_magnitude_bit(coefficients + index, bitplane);
                        dic_j2k_ebcot_decode_sign(symbols, state, width, height, x, y + row);
                        dic_j2k_ebcot_apply_sign(coefficients + index, state[index].negative);
                        y += row + 1u;
                        continue;
                    }
                }

                context = dic_j2k_ebcot_significance_context(state, width, height, x, y, orientation);
                bit = dic_j2k_ebcot_take_decision(symbols, context);
                if (bit != 0u)
                {
                    dic_j2k_ebcot_set_magnitude_bit(coefficients + index, bitplane);
                    dic_j2k_ebcot_decode_sign(symbols, state, width, height, x, y);
                    dic_j2k_ebcot_apply_sign(coefficients + index, state[index].negative);
                }
                ++y;
            }
        }
    }
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex D.3 and Annex C.4, decoder reconstructs state-driven pass order from the MQ decision stream. */
dic_status dic_j2k_ebcot_decode_codeblock_rect(
    const dic_j2k_codeblock_stream *stream,
    uint32_t width,
    uint32_t height,
    dic_j2k_subband_orientation orientation,
    int32_t *coefficients
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_ebcot_state *state = NULL;
    dic_j2k_mq_context_state initial_contexts[DIC_J2K_EBCOT_CONTEXT_COUNT];
    dic_j2k_mq_decoder_session session;
    dic_j2k_ebcot_decoder_symbols symbols;
    size_t coefficient_count;
    uint32_t plane;
    dic_status status;

    if (stream == NULL || coefficients == NULL || width == 0u || height == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (orientation > DIC_J2K_SUBBAND_HH)
        return DIC_STATUS_INVALID_ARGUMENT;
    if ((size_t)width > (size_t)-1 / height)
        return DIC_STATUS_INVALID_ARGUMENT;

    coefficient_count = (size_t)width * height;
    state = (dic_j2k_ebcot_state *)calloc(coefficient_count, sizeof(state[0]));
    if (state == NULL)
    {
        free(state);
        return DIC_STATUS_MEMORY_ERROR;
    }

    memset(coefficients, 0, coefficient_count * sizeof(coefficients[0]));
    dic_j2k_ebcot_initial_contexts(initial_contexts);
    status = dic_j2k_mq_decoder_session_init(
        &session,
        &stream->mq,
        initial_contexts,
        DIC_J2K_EBCOT_CONTEXT_COUNT
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
                    dic_j2k_ebcot_decode_sigprop_pass(
                        &symbols,
                        coefficients,
                        state,
                        width,
                        height,
                        orientation,
                        bitplane
                    );
                    dic_j2k_ebcot_decode_magref_pass(
                        &symbols,
                        coefficients,
                        state,
                        width,
                        height,
                        bitplane
                    );
                }
                dic_j2k_ebcot_decode_cleanup_pass(
                    &symbols,
                    coefficients,
                    state,
                    width,
                    height,
                    orientation,
                    bitplane
                );
                dic_j2k_ebcot_clear_pass_flags(state, coefficient_count);
            }
        }
        status = symbols.status;
        if (status == DIC_STATUS_OK && session.decisions_decoded != stream->mq.bit_count)
            status = DIC_J2K_MALFORMED_ARITHMETIC_STREAM;
    }

    free(state);
    return status;
}

dic_status dic_j2k_ebcot_decode_codeblock(
    const dic_j2k_codeblock_stream *stream,
    size_t coefficient_count,
    int32_t *coefficients
)
{
    DIC_J2K_DEBUG_ENTER();
    if (coefficient_count > UINT32_MAX)
        return DIC_STATUS_INVALID_ARGUMENT;
    return dic_j2k_ebcot_decode_codeblock_rect(
        stream,
        (uint32_t)coefficient_count,
        1u,
        DIC_J2K_SUBBAND_LL_LH,
        coefficients
    );
}

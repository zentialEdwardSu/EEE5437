#pragma once

/**
 * @file j2k_ebcot.h
 * @brief JPEG 2000 Embedded Block Coding with Optimized Truncation (EBCOT) declarations.
 *
 * Implements T.800 Annex D — the EBCOT algorithm that entropy-codes each
 * code-block's wavelet coefficients into compressed bit-stream contributions.
 * EBCOT is the core coding engine of JPEG 2000 and operates independently on
 * every code-block (typically 64×64 coefficients) within every sub-band.
 *
 * The coding process (Annex D.2-D.5) operates per code-block:
 *   -# Coefficients are organised into bit-planes from most-significant
 *      magnitude bit-plane (floor(log2(max|coeff|))) down to 1.
 *   -# Each bit-plane is scanned in four-row vertical stripes (D.2).
 *   -# Three coding passes visit each bit-plane (D.3):
 *        - Significance Propagation (SP): neighbours of already-significant
 *          coefficients
 *        - Magnitude Refinement (MR): already-significant coefficients
 *        - Cleanup (CU): all remaining coefficients (always coded)
 *   -# Context-adaptive binary arithmetic coding (D.4) uses 19 context models
 *      selected by significance neighbourhood and sub-band orientation.
 *   -# Each coding pass is terminated via the MQ FLUSH procedure, producing
 *      truncation points that enable rate-distortion layer construction.
 *
 * The j2k_codeblock_stream structure holds the compressed bytes and per-pass
 * metadata (length, decision count, distortion reduction, RD slope) required
 * for packet construction and decoder reconstruction.
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex D.1 (overview)
 * - paper/T-REC-T.800-200208.pdf, Annex D.2 (coefficient scanning and striping)
 * - paper/T-REC-T.800-200208.pdf, Annex D.3 (context models and coding passes)
 * - paper/T-REC-T.800-200208.pdf, Annex D.4 (arithmetic coding of code-block data)
 * - paper/T-REC-T.800-200208.pdf, Annex D.5 (truncation points and termination)
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"
#include "j2k/j2k_mq.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compressed code-block stream with per-pass truncation metadata.
 *
 * Holds the MQ-coded contribution for one JPEG 2000 code-block, together
 * with the rate-distortion information needed for quality-layer construction.
 */
typedef struct j2k_codeblock_stream
{
    /** MQ-coded contribution for one JPEG 2000 code-block. */
    j2k_mq_stream mq;
    /** Byte length of each terminated coding-pass segment in mq.data order. */
    size_t *pass_lengths;
    /** Number of MQ decisions encoded in each coding-pass segment. */
    size_t *pass_decision_counts;
    /** Coefficient-domain squared-error reduction contributed by each pass. */
    double *pass_distortion_reductions;
    /** Rate-distortion slope for each terminated pass (distortion reduction per byte). */
    double *pass_rd_slopes;
    /** Number of leading insignificant magnitude bit-planes (tag-tree zero bit-planes). */
    uint32_t zero_bitplanes;
    /** Number of EBCOT coding passes represented in the stream. */
    uint32_t coding_passes;
    /** Number of magnitude bit-planes present before leading-zero suppression. */
    uint32_t magnitude_bitplanes;
    /** Code-block width in coefficients. */
    uint32_t width;
    /** Code-block height in coefficients. */
    uint32_t height;
    /** Sub-band orientation (j2k_SUBBAND_LL_LH, j2k_SUBBAND_HL, or j2k_SUBBAND_HH)
     *  used to select context models per Annex D.3. */
    uint8_t subband_orientation;
} j2k_codeblock_stream;

/**
 * @brief EBCOT sub-band orientation for context model selection.
 *
 * EBCOT defines three context model groups (Annex D.3, Tables D.1-D.3):
 * LL/LH bands share one group (horizontal low-pass), HL uses a second,
 * and HH uses a third. This enum maps the four DWT sub-band orientations
 * onto these three context groups.
 */
typedef enum j2k_subband_orientation
{
    /** LL or LH sub-band: horizontal low-pass context group. */
    j2k_SUBBAND_LL_LH = 0,
    /** HL sub-band: vertical low-pass context group. */
    j2k_SUBBAND_HL = 1,
    /** HH sub-band: both high-pass context group. */
    j2k_SUBBAND_HH = 2
} j2k_subband_orientation;

/**
 * @brief Initialise a code-block stream to an empty state.
 *
 * @param stream Code-block stream to initialise; NULL is silently ignored.
 */
void j2k_codeblock_stream_init(j2k_codeblock_stream *stream);

/**
 * @brief Release all memory owned by a code-block stream.
 *
 * Frees the MQ data buffer and per-pass metadata arrays, then
 * reinitialises the stream to the empty state.
 *
 * @param stream Code-block stream to free; NULL is silently ignored.
 */
void j2k_codeblock_stream_free(j2k_codeblock_stream *stream);

/**
 * @brief EBCOT-encode one code-block with rectangular geometry.
 *
 * The caller provides a coefficient rectangle (width × height) and
 * sub-band orientation. Coefficients are accessed in row-major order.
 * This is the main encoder entry point; it handles coefficient scanning,
 * context model selection, MQ arithmetic coding, and pass termination.
 *
 * @param coefficients Row-major array of code-block coefficients (width × height).
 * @param width Code-block width in coefficients (> 0, ≤ j2k_EBCOT_MAX_CODEBLOCK_SIZE).
 * @param height Code-block height in coefficients (> 0, ≤ j2k_EBCOT_MAX_CODEBLOCK_SIZE).
 * @param orientation Sub-band orientation for context model selection.
 * @param stream [out] Receives the encoded stream with per-pass metadata.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT for NULL or out-of-range parameters.
 * @return DIC_STATUS_MEMORY_ERROR if MQ or metadata allocation fails.
 */
dic_status j2k_ebcot_encode_codeblock_rect(
    const int32_t *coefficients,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    j2k_codeblock_stream *stream
);

/**
 * @brief EBCOT-encode one code-block as a single non-terminated MQ segment.
 *
 * This emits COD code-block style 0 compatible data: all coding passes are
 * MQ-coded in one stream and packetized with a single aggregate codeword
 * length. It does not provide per-pass truncation lengths.
 */
dic_status j2k_ebcot_encode_codeblock_rect_aggregate(
    const int32_t *coefficients,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    j2k_codeblock_stream *stream
);

/**
 * @brief EBCOT-encode a linear coefficient array.
 *
 * Backward-compatible wrapper that treats coefficients as a linear
 * array. Prefer j2k_ebcot_encode_codeblock_rect() for code-blocks
 * with known dimensions; the orientation context models then apply
 * correctly.
 *
 * @param coefficients Linear array of coefficients.
 * @param coefficient_count Total number of coefficients.
 * @param stream [out] Receives the encoded stream.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_ebcot_encode_codeblock(
    const int32_t *coefficients,
    size_t coefficient_count,
    j2k_codeblock_stream *stream
);

/**
 * @brief EBCOT-decode one code-block with rectangular geometry.
 *
 * Reconstructs coefficients from the compressed coding-pass data in
 * @p stream. The decoder replays the MQ arithmetic decoding, using the
 * same context models and scanning order as the encoder.
 *
 * @param stream Encoded code-block stream with per-pass metadata.
 * @param width Code-block width in coefficients.
 * @param height Code-block height in coefficients.
 * @param orientation Sub-band orientation for context model selection.
 * @param coefficients [out] Row-major array receiving reconstructed
 *                     coefficients (width × height). Must be pre-allocated.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT for NULL or invalid parameters.
 */
dic_status j2k_ebcot_decode_codeblock_rect(
    const j2k_codeblock_stream *stream,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    int32_t *coefficients
);

/**
 * @brief EBCOT-decode a linear coefficient array from a stream.
 *
 * Backward-compatible wrapper. Prefer j2k_ebcot_decode_codeblock_rect().
 *
 * @param stream Encoded code-block stream.
 * @param coefficient_count Total number of coefficients to reconstruct.
 * @param coefficients [out] Receives reconstructed coefficients.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_ebcot_decode_codeblock(
    const j2k_codeblock_stream *stream,
    size_t coefficient_count,
    int32_t *coefficients
);

#ifdef __cplusplus
}
#endif

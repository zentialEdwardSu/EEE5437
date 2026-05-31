#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"
#include "j2k/j2k_mq.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct j2k_codeblock_stream
{
    j2k_mq_stream mq; /**< MQ-coded contribution for one JPEG 2000 code-block. */
    size_t *pass_lengths; /**< Byte length of each terminated coding-pass segment in mq.data order. */
    size_t *pass_decision_counts; /**< Number of MQ decisions encoded in each coding-pass segment. */
    double *pass_distortion_reductions; /**< Coefficient-domain squared-error reduction contributed by each pass. */
    double *pass_rd_slopes; /**< Rate-distortion slope for each terminated pass, measured as distortion reduction per byte. */
    uint32_t zero_bitplanes; /**< Number of leading insignificant magnitude bit-planes. */
    uint32_t coding_passes; /**< Number of EBCOT coding passes represented in the stream. */
    uint32_t magnitude_bitplanes; /**< Number of magnitude bit-planes present before leading-zero suppression. */
    uint32_t width; /**< Code-block width in coefficients. */
    uint32_t height; /**< Code-block height in coefficients. */
    uint8_t subband_orientation; /**< One j2k_subband_orientation value used for Annex D contexts. */
} j2k_codeblock_stream;

typedef enum j2k_subband_orientation
{
    j2k_SUBBAND_LL_LH = 0,
    j2k_SUBBAND_HL = 1,
    j2k_SUBBAND_HH = 2
} j2k_subband_orientation;

void j2k_codeblock_stream_init(j2k_codeblock_stream *stream);
void j2k_codeblock_stream_free(j2k_codeblock_stream *stream);

dic_status j2k_ebcot_encode_codeblock_rect(
    const int32_t *coefficients,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    j2k_codeblock_stream *stream
);

dic_status j2k_ebcot_encode_codeblock(
    const int32_t *coefficients,
    size_t coefficient_count,
    j2k_codeblock_stream *stream
);

dic_status j2k_ebcot_decode_codeblock_rect(
    const j2k_codeblock_stream *stream,
    uint32_t width,
    uint32_t height,
    j2k_subband_orientation orientation,
    int32_t *coefficients
);

dic_status j2k_ebcot_decode_codeblock(
    const j2k_codeblock_stream *stream,
    size_t coefficient_count,
    int32_t *coefficients
);

#ifdef __cplusplus
}
#endif

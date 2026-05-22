#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"
#include "j2k/dic_j2k_mq.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_j2k_codeblock_stream
{
    dic_j2k_mq_stream mq; /**< MQ-coded contribution for one JPEG 2000 code-block. */
    uint32_t zero_bitplanes; /**< Number of leading insignificant magnitude bit-planes. */
    uint32_t coding_passes; /**< Number of EBCOT coding passes represented in the stream. */
    uint32_t magnitude_bitplanes; /**< Number of magnitude bit-planes present before leading-zero suppression. */
    uint32_t width; /**< Code-block width in coefficients. */
    uint32_t height; /**< Code-block height in coefficients. */
    uint8_t subband_orientation; /**< One dic_j2k_subband_orientation value used for Annex D contexts. */
} dic_j2k_codeblock_stream;

typedef enum dic_j2k_subband_orientation
{
    DIC_J2K_SUBBAND_LL_LH = 0,
    DIC_J2K_SUBBAND_HL = 1,
    DIC_J2K_SUBBAND_HH = 2
} dic_j2k_subband_orientation;

void dic_j2k_codeblock_stream_init(dic_j2k_codeblock_stream *stream);
void dic_j2k_codeblock_stream_free(dic_j2k_codeblock_stream *stream);

dic_status dic_j2k_ebcot_encode_codeblock_rect(
    const int32_t *coefficients,
    uint32_t width,
    uint32_t height,
    dic_j2k_subband_orientation orientation,
    dic_j2k_codeblock_stream *stream
);

dic_status dic_j2k_ebcot_encode_codeblock(
    const int32_t *coefficients,
    size_t coefficient_count,
    dic_j2k_codeblock_stream *stream
);

dic_status dic_j2k_ebcot_decode_codeblock_rect(
    const dic_j2k_codeblock_stream *stream,
    uint32_t width,
    uint32_t height,
    dic_j2k_subband_orientation orientation,
    int32_t *coefficients
);

dic_status dic_j2k_ebcot_decode_codeblock(
    const dic_j2k_codeblock_stream *stream,
    size_t coefficient_count,
    int32_t *coefficients
);

#ifdef __cplusplus
}
#endif

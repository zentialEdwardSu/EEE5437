#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "errors/errors.h"
#include "j2k/j2k_ebcot.h"

#ifdef __cplusplus
extern "C" {
#endif

enum j2k_marker
{
    j2k_MARKER_SOC = 0xff4f,
    j2k_MARKER_SIZ = 0xff51,
    j2k_MARKER_COD = 0xff52,
    j2k_MARKER_RGN = 0xff5e,
    j2k_MARKER_QCD = 0xff5c,
    j2k_MARKER_QCC = 0xff5d,
    j2k_MARKER_SOT = 0xff90,
    j2k_MARKER_SOP = 0xff91,
    j2k_MARKER_EPH = 0xff92,
    j2k_MARKER_SOD = 0xff93,
    j2k_MARKER_EOC = 0xffd9
};

enum
{
    j2k_MAX_DECOMPOSITION_LEVELS = 32,
    j2k_MAX_QUANT_STEPS = 1 + 3 * j2k_MAX_DECOMPOSITION_LEVELS
};

typedef struct j2k_basic_params
{
    uint32_t width; /**< Reference-grid image width signalled as Xsiz in the SIZ marker. */
    uint32_t height; /**< Reference-grid image height signalled as Ysiz in the SIZ marker. */
    uint16_t components; /**< Number of image components signalled as Csiz in the SIZ marker. */
    uint8_t decomposition_levels; /**< Number of wavelet decomposition levels signalled in COD SPcod. */
    uint8_t reversible; /**< Non-zero selects the reversible 5-3 transform; zero selects the irreversible style. */
    uint8_t multiple_component_transform; /**< Non-zero enables the COD multiple-component transform flag. */
    uint16_t layers; /**< Number of quality layers signalled as SGcod L in the COD marker segment. */
    uint32_t tile_width; /**< Tile width XTsiz; zero means use the full image width as one tile. */
    uint32_t tile_height; /**< Tile height YTsiz; zero means use the full image height as one tile. */
    uint8_t roi_shift; /**< RGN MAXSHIFT scaling value; zero omits the RGN marker segment. */
    uint8_t use_sop; /**< Non-zero sets COD Scod bit 1; packet payloads must contain SOP marker segments. */
    uint8_t use_eph; /**< Non-zero sets COD Scod bit 2; packet payloads must contain EPH markers after each packet header. */
    uint8_t use_precincts; /**< Non-zero sets COD Scod bit 0 and writes explicit maximum precinct bytes. */
    uint8_t precinct_width_exponents[j2k_MAX_DECOMPOSITION_LEVELS + 1]; /**< PPx per resolution; writer currently accepts only 15. */
    uint8_t precinct_height_exponents[j2k_MAX_DECOMPOSITION_LEVELS + 1]; /**< PPy per resolution; writer currently accepts only 15. */
    uint8_t quant_guard_bits; /**< QCD guard-bit count used to derive packet-header nominal bit-plane counts. */
    uint16_t quant_step_count; /**< Number of default quantization step sizes parsed from or written to QCD. */
    double quant_step_sizes[j2k_MAX_QUANT_STEPS]; /**< Irreversible scalar expounded QCD step sizes in subband order. */
} j2k_basic_params;

typedef struct j2k_tile_part_payload
{
    uint16_t tile_index; /**< Isot tile index for this tile-part. */
    uint8_t tile_part_index; /**< TPsot tile-part index within the tile. */
    uint8_t tile_part_count; /**< TNsot total tile-parts for this tile; zero means unknown. */
    const uint8_t *payload; /**< Compressed tile-part bytes following SOD. */
    size_t payload_size; /**< Number of bytes in payload. */
} j2k_tile_part_payload;

dic_status j2k_write_minimal_codestream(
    const char *path,
    const j2k_basic_params *params
);

dic_status j2k_write_minimal_codestream_stream(
    FILE *file,
    const j2k_basic_params *params
);

dic_status j2k_write_codestream_with_payload(
    const char *path,
    const j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
);

dic_status j2k_write_codestream_with_payload_stream(
    FILE *file,
    const j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
);

dic_status j2k_write_codestream_with_tile_parts(
    const char *path,
    const j2k_basic_params *params,
    const j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
);

dic_status j2k_write_codestream_with_tile_parts_stream(
    FILE *file,
    const j2k_basic_params *params,
    const j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
);

dic_status j2k_write_empty_packet_codestream(
    const char *path,
    const j2k_basic_params *params
);

dic_status j2k_write_empty_packet_codestream_stream(
    FILE *file,
    const j2k_basic_params *params
);

dic_status j2k_write_ebcot_packet_codestream(
    const char *path,
    const j2k_basic_params *params,
    const j2k_codeblock_stream *streams,
    size_t stream_count
);

dic_status j2k_write_ebcot_packet_codestream_stream(
    FILE *file,
    const j2k_basic_params *params,
    const j2k_codeblock_stream *streams,
    size_t stream_count
);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "errors/errors.h"
#include "j2k/dic_j2k_ebcot.h"

#ifdef __cplusplus
extern "C" {
#endif

enum dic_j2k_marker
{
    DIC_J2K_MARKER_SOC = 0xff4f,
    DIC_J2K_MARKER_SIZ = 0xff51,
    DIC_J2K_MARKER_COD = 0xff52,
    DIC_J2K_MARKER_RGN = 0xff5e,
    DIC_J2K_MARKER_QCD = 0xff5c,
    DIC_J2K_MARKER_QCC = 0xff5d,
    DIC_J2K_MARKER_SOT = 0xff90,
    DIC_J2K_MARKER_SOD = 0xff93,
    DIC_J2K_MARKER_EOC = 0xffd9
};

typedef struct dic_j2k_basic_params
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
} dic_j2k_basic_params;

dic_status dic_j2k_write_minimal_codestream(
    const char *path,
    const dic_j2k_basic_params *params
);

dic_status dic_j2k_write_minimal_codestream_stream(
    FILE *file,
    const dic_j2k_basic_params *params
);

dic_status dic_j2k_write_codestream_with_payload(
    const char *path,
    const dic_j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
);

dic_status dic_j2k_write_codestream_with_payload_stream(
    FILE *file,
    const dic_j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
);

dic_status dic_j2k_write_empty_packet_codestream(
    const char *path,
    const dic_j2k_basic_params *params
);

dic_status dic_j2k_write_empty_packet_codestream_stream(
    FILE *file,
    const dic_j2k_basic_params *params
);

dic_status dic_j2k_write_ebcot_packet_codestream(
    const char *path,
    const dic_j2k_basic_params *params,
    const dic_j2k_codeblock_stream *streams,
    size_t stream_count
);

dic_status dic_j2k_write_ebcot_packet_codestream_stream(
    FILE *file,
    const dic_j2k_basic_params *params,
    const dic_j2k_codeblock_stream *streams,
    size_t stream_count
);

#ifdef __cplusplus
}
#endif

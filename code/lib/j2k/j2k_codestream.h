#pragma once

/**
 * @file j2k_codestream.h
 * @brief JPEG 2000 codestream writing API.
 *
 * Implements T.800 Annex A — the syntax of a raw JPEG 2000 codestream
 * as a sequence of marker segments and marker-delimited tile-part data.
 * The codestream structure (Annex A.3) is:
 *
 * @code
 *   SOC                                 (start of codestream)
 *   SIZ                                 (image and tile size)
 *   [COD] [QCD] [RGN] ...               (main-header marker segments)
 *   SOT  [tile-part header markers] SOD (tile-part 1)
 *     ... tile-part payload bytes ...
 *   SOT  [tile-part header markers] SOD (tile-part 2, if tiled)
 *     ...
 *   EOC                                 (end of codestream)
 * @endcode
 *
 * Marker segments supported:
 * - SOC (0xFF4F): Start of Codestream (Annex A.4.1)
 * - SIZ (0xFF51): Image and Tile Size (Annex A.5.1, Table A.9)
 * - COD (0xFF52): Coding Style Default (Annex A.6.1, Figure A.9)
 * - QCD (0xFF5C): Quantization Default (Annex A.6.4)
 * - RGN (0xFF5E): Region of Interest (Annex A.8.4, Tables A.24-A.26)
 * - SOT (0xFF90): Start of Tile-part (Annex A.4.2, Table A.5)
 * - SOP (0xFF91): Start of Packet (Annex B.10.6)
 * - EPH (0xFF92): End of Packet Header (Annex B.10.7)
 * - SOD (0xFF93): Start of Data (Annex A.4.3)
 * - EOC (0xFFD9): End of Codestream (Annex A.4.4)
 *
 * This module provides functions to assemble and write complete
 * codestreams with payloads produced by the encoder or local tests.
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex A.1-A.8 (codestream syntax)
 * - paper/T-REC-T.800-200208.pdf, Annex A.4 (delimiting markers)
 * - paper/T-REC-T.800-200208.pdf, Annex A.5 (fixed information markers)
 * - paper/T-REC-T.800-200208.pdf, Annex A.6 (functional markers)
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "errors/errors.h"
#include "j2k/j2k_ebcot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JPEG 2000 marker codes (Annex A, Table A.1).
 *
 * All markers are 2-byte big-endian values with the first byte 0xFF.
 */
enum j2k_marker
{
    /** Start of Codestream (Annex A.4.1). */
    j2k_MARKER_SOC = 0xff4f,
    /** Image and Tile Size (Annex A.5.1, Table A.9). */
    j2k_MARKER_SIZ = 0xff51,
    /** Coding Style Default (Annex A.6.1, Figure A.9). */
    j2k_MARKER_COD = 0xff52,
    /** Region of Interest (Annex A.8.4, Tables A.24-A.26). */
    j2k_MARKER_RGN = 0xff5e,
    /** Quantization Default (Annex A.6.4). */
    j2k_MARKER_QCD = 0xff5c,
    /** Quantization Component (Annex A.6.5). */
    j2k_MARKER_QCC = 0xff5d,
    /** Start of Tile-part (Annex A.4.2, Table A.5). */
    j2k_MARKER_SOT = 0xff90,
    /** Start of Packet (Annex B.10.6). */
    j2k_MARKER_SOP = 0xff91,
    /** End of Packet Header (Annex B.10.7). */
    j2k_MARKER_EPH = 0xff92,
    /** Start of Data (Annex A.4.3). */
    j2k_MARKER_SOD = 0xff93,
    /** End of Codestream (Annex A.4.4). */
    j2k_MARKER_EOC = 0xffd9
};

/** Maximum decomposition levels per Annex A, Profile-0 limit. */
enum
{
    /** Maximum allowed decomposition levels (Annex A.6.1). */
    j2k_MAX_DECOMPOSITION_LEVELS = 32,
    /** Maximum QCD/QCC step entries: 1 LL + 3 per level. */
    j2k_MAX_QUANT_STEPS = 1 + 3 * j2k_MAX_DECOMPOSITION_LEVELS
};

/**
 * @brief JPEG 2000 codestream parameters from SIZ, COD, QCD, and RGN.
 *
 * This structure holds all marker-segment parameters needed to write
 * or parse a codestream. Fields are named after the corresponding
 * Annex A marker fields.
 */
typedef struct j2k_basic_params
{
    /** Reference-grid image width (SIZ Xsiz, Annex A.5.1). */
    uint32_t width;
    /** Reference-grid image height (SIZ Ysiz, Annex A.5.1). */
    uint32_t height;
    /** Number of image components (SIZ Csiz, 1 or 3). */
    uint16_t components;
    /** Number of wavelet decomposition levels (COD SPcod, Annex A.6.1). */
    uint8_t decomposition_levels;
    /** Non-zero: reversible 5-3 transform; zero: irreversible 9-7 (COD SPcod). */
    uint8_t reversible;
    /** Non-zero: enable multiple-component transform (COD SPcod MCT). */
    uint8_t multiple_component_transform;
    /** Number of quality layers (COD SGcod, Annex A.6.1). */
    uint16_t layers;
    /** Code-block coding style flags (COD SPcod, Table A.20). */
    uint8_t codeblock_style;
    /** Tile width (SIZ XTsiz); 0 means single-tile (full image width). */
    uint32_t tile_width;
    /** Tile height (SIZ YTsiz); 0 means single-tile (full image height). */
    uint32_t tile_height;
    /** ROI Maxshift scaling (RGN SPrgn); 0 omits the RGN marker. */
    uint8_t roi_shift;
    /** Non-zero: set COD Scod bit 1 (SOP marker in each packet). */
    uint8_t use_sop;
    /** Non-zero: set COD Scod bit 2 (EPH marker after each packet header). */
    uint8_t use_eph;
    /** Non-zero: set COD Scod bit 0 (explicit precinct sizes). */
    uint8_t use_precincts;
    /** Precinct width exponents PPx per resolution (COD SPcod). */
    uint8_t precinct_width_exponents[j2k_MAX_DECOMPOSITION_LEVELS + 1];
    /** Precinct height exponents PPy per resolution (COD SPcod). */
    uint8_t precinct_height_exponents[j2k_MAX_DECOMPOSITION_LEVELS + 1];
    /** Guard bits for irreversible QCD (SQcd). */
    uint8_t quant_guard_bits;
    /** Number of quantization step entries in QCD. */
    uint16_t quant_step_count;
    /** Irreversible quantization step sizes (expounded SPqcd fields). */
    double quant_step_sizes[j2k_MAX_QUANT_STEPS];
} j2k_basic_params;

/**
 * @brief Descriptor for one tile-part's compressed payload.
 *
 * Passed to the multi-tile codestream writer to assemble
 * the complete SOT/SOD/tile-data sequence.
 */
typedef struct j2k_tile_part_payload
{
    /** Tile index (SOT Isot, Annex A.4.2). */
    uint16_t tile_index;
    /** Tile-part index within the tile (SOT TPsot). */
    uint8_t tile_part_index;
    /** Total tile-parts for this tile (SOT TNsot); 0 means unknown. */
    uint8_t tile_part_count;
    /** Compressed tile-part bytes following SOD. */
    const uint8_t *payload;
    /** Number of bytes in @p payload. */
    size_t payload_size;
} j2k_tile_part_payload;

/**
 * @brief Write a minimal codestream (SOD/EOC with no packets).
 *
 * Writes SOC, SIZ, COD, QCD, SOT, SOD, EOC with zero-length tile-part
 * payload. Used for testing the marker-segment writer.
 *
 * @param path Output file path.
 * @param params Codestream parameters.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_write_minimal_codestream(
    const char *path,
    const j2k_basic_params *params
);

/** @brief Stream variant of j2k_write_minimal_codestream(). */
dic_status j2k_write_minimal_codestream_stream(
    FILE *file,
    const j2k_basic_params *params
);

/**
 * @brief Write a single-tile codestream with caller-supplied payload.
 *
 * Emits SOC → SIZ → COD → QCD → [RGN] → SOT → SOD → @p payload → EOC.
 * The payload bytes follow SOD immediately and represent one tile-part.
 *
 * @param path Output file path.
 * @param params Codestream parameters (must match the payload).
 * @param payload Encoded tile-part bytes.
 * @param payload_size Number of payload bytes.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_write_codestream_with_payload(
    const char *path,
    const j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
);

/** @brief Stream variant of j2k_write_codestream_with_payload(). */
dic_status j2k_write_codestream_with_payload_stream(
    FILE *file,
    const j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
);

/**
 * @brief Write a multi-tile codestream.
 *
 * Emits SOC, main-header markers, then for each tile-part:
 * SOT → [tile-part-specific markers] → SOD → payload. Terminates with
 * EOC. The SOT Psot field is computed from the payload size.
 *
 * @param path Output file path.
 * @param params Codestream parameters for the main header.
 * @param tile_parts Array of tile-part descriptors.
 * @param tile_part_count Number of entries in @p tile_parts.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_write_codestream_with_tile_parts(
    const char *path,
    const j2k_basic_params *params,
    const j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
);

/** @brief Stream variant of j2k_write_codestream_with_tile_parts(). */
dic_status j2k_write_codestream_with_tile_parts_stream(
    FILE *file,
    const j2k_basic_params *params,
    const j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
);

/**
 * @brief Write a codestream with empty packet headers (LRCP, zero payload).
 *
 * Outputs SOC, SIZ, COD, QCD, then the proper number of SOT/SOD tile-parts
 * each containing zero-length SOP/EPH packet pairs. Terminates with EOC.
 *
 * @param path Output file path.
 * @param params Codestream parameters.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_write_empty_packet_codestream(
    const char *path,
    const j2k_basic_params *params
);

/** @brief Stream variant of j2k_write_empty_packet_codestream(). */
dic_status j2k_write_empty_packet_codestream_stream(
    FILE *file,
    const j2k_basic_params *params
);

/**
 * @brief Write a codestream with one packet from EBCOT code-block streams.
 *
 * Encodes all code-block streams into a single quality-layer packet
 * and wraps them in a single-tile codestream.
 *
 * @param path Output file path.
 * @param params Codestream parameters.
 * @param streams EBCOT code-block streams.
 * @param stream_count Number of streams.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_write_ebcot_packet_codestream(
    const char *path,
    const j2k_basic_params *params,
    const j2k_codeblock_stream *streams,
    size_t stream_count
);

/** @brief Stream variant of j2k_write_ebcot_packet_codestream(). */
dic_status j2k_write_ebcot_packet_codestream_stream(
    FILE *file,
    const j2k_basic_params *params,
    const j2k_codeblock_stream *streams,
    size_t stream_count
);

#ifdef __cplusplus
}
#endif

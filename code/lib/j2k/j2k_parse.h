#pragma once

/**
 * @file j2k_parse.h
 * @brief JPEG 2000 codestream and JP2 file parsing API.
 *
 * Provides metadata extraction from raw J2K codestreams and JP2-wrapped
 * files. The parser reads marker segments (SOC, SIZ, COD, QCD, RGN, SOT,
 * SOD, EOC) and JP2 boxes to populate a j2k_codestream_info structure
 * without decoding packet bodies. This is used for project-level validation
 * and informational display.
 *
 * Parsed marker segments:
 * - SIZ (Annex A.5.1, Table A.9): image dimensions, tile size, component count
 * - COD (Annex A.6.1, Figure A.9): coding style, decomposition levels,
 *   layers, MCT flag, precinct sizes
 * - QCD (Annex A.6.4): quantization defaults — reversible exponent bytes
 *   or irreversible expounded SPqcd fields
 * - RGN (Annex A.8.4, Tables A.24-A.26): ROI Maxshift signalling
 * - SOT (Annex A.4.2, Table A.5): tile-part header with Isot, Psot
 * - SOD (Annex A.4.3): marks start of tile-part data; the parser skips
 *   payload bytes using Psot
 *
 * Unsupported marker segments are skipped using their Annex A length fields
 * without error.
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex A (codestream syntax)
 * - paper/T-REC-T.800-200208.pdf, Annex I.5 (JP2 boxes)
 * - j2k_codestream.h for parameter struct definitions
 */

#include <stddef.h>

#include "j2k/j2k_codestream.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Accumulated codestream metadata from marker-segment parsing.
 *
 * Holds the parsed SIZ/COD/RGN parameters and counts of tile-part
 * payloads encountered during codestream traversal.
 */
typedef struct j2k_codestream_info
{
    /** Parsed SIZ/COD/RGN/QCD parameters from the main and tile-part headers. */
    j2k_basic_params params;
    /** Number of compressed bytes after the most recent parsed SOD (from Psot - 14). */
    size_t tile_part_payload_bytes;
    /** Running sum of all tile-part payload bytes (excluding SOT/SOD headers). */
    size_t total_tile_part_payload_bytes;
    /** Number of SOT/SOD tile-parts found in the codestream. */
    uint32_t tile_part_count;
    /** Isot value from the most recently parsed SOT marker segment. */
    uint16_t last_tile_index;
} j2k_codestream_info;

/**
 * @brief Read and parse codestream marker segments from a raw J2K file.
 *
 * Opens the file, verifies the SOC marker, and walks marker segments
 * (SIZ, COD, QCD, RGN, SOT, SOD, EOC) to populate @p info. SOD tile-part
 * payloads are skipped using Psot. Returns after EOC.
 *
 * @param path Path to a raw J2K codestream file.
 * @param info [out] Receives parsed codestream parameters and statistics.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p path or @p info is NULL.
 * @return DIC_STATUS_FILE_OPEN_ERROR if the file cannot be opened.
 * @return DIC_J2K_FORMAT_ERROR if the codestream syntax is invalid.
 */
dic_status j2k_read_codestream_info(
    const char *path,
    j2k_codestream_info *info
);

/**
 * @brief Read and parse codestream info from a JP2-wrapped file.
 *
 * Traverses JP2 boxes (Annex I.5.2.1) to locate the Contiguous Codestream
 * box (jp2c), then delegates to j2k_read_codestream_info for marker parsing.
 *
 * @param path Path to a JP2 file.
 * @param info [out] Receives parsed codestream parameters and statistics.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p path or @p info is NULL.
 * @return DIC_STATUS_FILE_OPEN_ERROR if the file cannot be opened.
 * @return DIC_J2K_FORMAT_ERROR if the JP2 or codestream syntax is invalid.
 */
dic_status jp2_read_codestream_info(
    const char *path,
    j2k_codestream_info *info
);

#ifdef __cplusplus
}
#endif

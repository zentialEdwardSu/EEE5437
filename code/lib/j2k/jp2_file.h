#pragma once

/**
 * @file jp2_file.h
 * @brief JPEG 2000 Part 1 file format (JP2) declarations.
 *
 * Implements T.800 Annex I — the JP2 file format wrapper around a raw
 * J2K codestream. A minimal JP2 file contains, in order:
 *   - JP   Signature box (12 bytes): magic value 0x0D0A870A
 *   - FTYP File Type box (Annex I.5.2): brand "jp2 " and compatibility
 *   - JP2H JP2 Header superbox (Annex I.5.3):
 *       - IHDR Image Header (Annex I.5.3.1, Table I.5)
 *       - COLR Colour Specification (Annex I.5.3.3, Table I.7)
 *   - JP2C Contiguous Codestream box (Annex I.5.2.1)
 *
 * Box structure (Annex I.4):
 *   Each box has an 8-byte header: 4-byte big-endian length (LBox)
 *   followed by a 4-byte type code (TBox). A LBox of 0 indicates the
 *   box extends to end-of-file.
 *
 * This module emits the minimal JP2 profile — it does not write optional
 * boxes such as XML, UUID, palette, channel definition, or IPR.
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex I (JP2 file format)
 * - paper/T-REC-T.800-200208.pdf, Annex I.5.1 (Signature box)
 * - paper/T-REC-T.800-200208.pdf, Annex I.5.2 (File Type box)
 * - paper/T-REC-T.800-200208.pdf, Annex I.5.3 (JP2 Header box)
 * - paper/T-REC-T.800-200208.pdf, Annex I.5.3.1 (Image Header box, Table I.5)
 * - paper/T-REC-T.800-200208.pdf, Annex I.5.3.3 (Colour Specification box, Table I.7)
 */

#include "j2k/j2k_codestream.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JP2 box type identifiers (TBox field, Annex I.4).
 */
enum jp2_box_type
{
    /** JP Signature box: magic 0x0D0A870A (Annex I.5.1). */
    jp2_BOX_JP   = 0x6a502020u,
    /** File Type box: brand and compatibility list (Annex I.5.2). */
    jp2_BOX_FTYP = 0x66747970u,
    /** JP2 Header superbox: contains IHDR and COLR (Annex I.5.3). */
    jp2_BOX_JP2H = 0x6a703268u,
    /** Image Header box: height, width, components, bit-depth (Annex I.5.3.1). */
    jp2_BOX_IHDR = 0x69686472u,
    /** Colour Specification box: method, precedence, enum (Annex I.5.3.3). */
    jp2_BOX_COLR = 0x636f6c72u,
    /** Contiguous Codestream box: contains the raw J2K codestream (Annex I.5.2.1). */
    jp2_BOX_JP2C = 0x6a703263u
};

/**
 * @brief Write a minimal JP2 file with an empty codestream.
 *
 * Writes all required JP2 boxes (JP, FTYP, JP2H/IHDR/COLR, JP2C)
 * followed by a codestream containing SOC, SIZ, COD, QCD, SOT, SOD,
 * and EOC with zero-length packet payload. Suitable for testing the
 * file-format wrapper independently from the encoder.
 *
 * @param path Output file path.
 * @param params Codestream parameters for IHDR, COD, and QCD emission.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT if @p path or @p params is NULL,
 *         dimensions are 0, or component count ≠ 1 or 3.
 * @return DIC_STATUS_IO_ERROR if file I/O fails.
 */
dic_status jp2_write_minimal_file(
    const char *path,
    const j2k_basic_params *params
);

/**
 * @brief Write a JP2 file wrapping an encoded single-tile codestream.
 *
 * Emits all JP2 boxes, then writes the codestream headers followed by
 * the caller-supplied payload (one tile-part). The JP2C box uses a LBox
 * of 0 (end-of-file length).
 *
 * @param path Output file path.
 * @param params Codestream parameters for headers and IHDR.
 * @param payload Encoded tile-part bytes following SOD.
 * @param payload_size Number of bytes in @p payload.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT for NULL/empty inputs.
 * @return DIC_STATUS_IO_ERROR if file I/O fails.
 */
dic_status jp2_write_file_with_codestream_payload(
    const char *path,
    const j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
);

/**
 * @brief Write a JP2 file wrapping an encoded multi-tile codestream.
 *
 * Same as jp2_write_file_with_codestream_payload() but accepts multiple
 * tile-part payloads for tiled encoding.
 *
 * @param path Output file path.
 * @param params Codestream parameters for headers and IHDR.
 * @param tile_parts Array of tile-part payload descriptors.
 * @param tile_part_count Number of entries in @p tile_parts.
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT for NULL/empty inputs.
 * @return DIC_STATUS_IO_ERROR if file I/O fails.
 */
dic_status jp2_write_file_with_codestream_tile_parts(
    const char *path,
    const j2k_basic_params *params,
    const j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
);

#ifdef __cplusplus
}
#endif

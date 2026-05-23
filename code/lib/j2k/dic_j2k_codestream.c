/**
 * @file dic_j2k_codestream.c
 * @brief Writes the JPEG 2000 core codestream marker stream defined by T.800 Annex A.
 *
 * This file implements the minimal SOC/SIZ/COD/RGN/QCD/SOT/SOD/EOC writer used by the
 * project tests and by the JP2 wrapper. It also assembles simple packet payloads produced
 * by the local EBCOT path. The implementation intentionally writes a constrained single-tile
 * codestream and does not attempt to expose every marker segment permitted by Annex A. When
 * SOP or EPH is requested through the COD Scod flags, callers are responsible for supplying
 * payload bytes that already contain those in-bit-stream markers at packet boundaries. The
 * writer also supports explicit COD maximum-precinct-size signalling while keeping packet
 * assembly constrained to the project's single-precinct layout. Multi-tile output is written
 * as one tile-part per tile by callers that provide per-tile payloads.
 *
 * References: dic_j2k_codestream.h for public parameters, dic_j2k_packet.h for Annex B
 * packet payload construction, dic_jp2_file.c for Annex I file wrapping, and T.800 Annex J
 * examples for packet and arithmetic-decoder interoperability checks.
 */

#include "j2k/dic_j2k_codestream.h"
#include "j2k/dic_j2k_debug.h"

#include "j2k/dic_j2k_packet.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A.2-A.4, codestream marker syntax. */
static FILE *dic_j2k_open_file(const char *path, const char *mode)
{
    DIC_J2K_DEBUG_ENTER();
    FILE *file = NULL;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, mode) != 0)
        return NULL;
    return file;
#else
    return fopen(path, mode);
#endif
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A, marker segment byte-stream syntax. */
static int dic_j2k_write_u8(FILE *file, uint8_t value)
{
    DIC_J2K_DEBUG_ENTER();
    return fputc((int)value, file) != EOF;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A tables list marker parameters as big-endian byte-stream fields. */
static int dic_j2k_write_u16_be(FILE *file, uint16_t value)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_write_u8(file, (uint8_t)(value >> 8))
        && dic_j2k_write_u8(file, (uint8_t)(value & 0xffu));
}

/* Reference: paper/T-REC-T.800-200208.pdf, Tables A.5 and A.9 define 32-bit Psot and geometry fields. */
static int dic_j2k_write_u32_be(FILE *file, uint32_t value)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_write_u8(file, (uint8_t)(value >> 24))
        && dic_j2k_write_u8(file, (uint8_t)((value >> 16) & 0xffu))
        && dic_j2k_write_u8(file, (uint8_t)((value >> 8) & 0xffu))
        && dic_j2k_write_u8(file, (uint8_t)(value & 0xffu));
}

/* Reference: paper/T-REC-T.800-200208.pdf, Table A.2, list of markers and marker segments. */
static int dic_j2k_write_marker(FILE *file, uint16_t marker)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_write_u16_be(file, marker);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.5.1, XTsiz/YTsiz define the regular tile grid over the reference grid. */
static dic_status dic_j2k_tile_grid(
    const dic_j2k_basic_params *params,
    uint32_t *tiles_x,
    uint32_t *tiles_y,
    uint32_t *tile_count
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t tile_width;
    uint32_t tile_height;
    uint64_t count;

    if (params == NULL || tiles_x == NULL || tiles_y == NULL || tile_count == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    tile_width = params->tile_width == 0u ? params->width : params->tile_width;
    tile_height = params->tile_height == 0u ? params->height : params->tile_height;
    if (tile_width == 0u || tile_height == 0u || tile_width > params->width || tile_height > params->height)
        return DIC_STATUS_INVALID_ARGUMENT;

    *tiles_x = (params->width + tile_width - 1u) / tile_width;
    *tiles_y = (params->height + tile_height - 1u) / tile_height;
    count = (uint64_t)(*tiles_x) * (uint64_t)(*tiles_y);
    if (count == 0u || count > UINT16_MAX + 1u)
        return DIC_STATUS_INVALID_ARGUMENT;

    *tile_count = (uint32_t)count;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.1 Table A.21, this encoder only supports the explicit 15/15 maximum precinct that preserves the single-precinct packet layout. */
static dic_status dic_j2k_validate_precincts(const dic_j2k_basic_params *params)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t resolution;

    if (!params->use_precincts)
        return DIC_STATUS_OK;

    for (resolution = 0u; resolution <= params->decomposition_levels; ++resolution)
    {
        uint8_t ppx = params->precinct_width_exponents[resolution];
        uint8_t ppy = params->precinct_height_exponents[resolution];

        if (ppx > 15u || ppy > 15u)
            return DIC_STATUS_INVALID_ARGUMENT;
        if (ppx != 15u || ppy != 15u)
            return DIC_J2K_UNSUPPORTED_PRECINCT_SIZE;
    }

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.3, SOD begins the tile-part bit-stream data. */
static int dic_j2k_write_bytes(FILE *file, const uint8_t *payload, size_t payload_size)
{
    DIC_J2K_DEBUG_ENTER();
    if (payload_size == 0u)
        return 1;
    return fwrite(payload, 1u, payload_size, file) == payload_size;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.5.1 and Table A.9, Image and tile size marker segment. */
static int dic_j2k_write_siz(FILE *file, const dic_j2k_basic_params *params)
{
    DIC_J2K_DEBUG_ENTER();
    uint16_t component;
    uint16_t length = (uint16_t)(38u + (uint16_t)params->components * 3u);
    uint32_t tile_width = params->tile_width == 0u ? params->width : params->tile_width;
    uint32_t tile_height = params->tile_height == 0u ? params->height : params->tile_height;

    if (!dic_j2k_write_marker(file, DIC_J2K_MARKER_SIZ)
        || !dic_j2k_write_u16_be(file, length)
        || !dic_j2k_write_u16_be(file, 0u)
        || !dic_j2k_write_u32_be(file, params->width)
        || !dic_j2k_write_u32_be(file, params->height)
        || !dic_j2k_write_u32_be(file, 0u)
        || !dic_j2k_write_u32_be(file, 0u)
        || !dic_j2k_write_u32_be(file, tile_width)
        || !dic_j2k_write_u32_be(file, tile_height)
        || !dic_j2k_write_u32_be(file, 0u)
        || !dic_j2k_write_u32_be(file, 0u)
        || !dic_j2k_write_u16_be(file, params->components))
    {
        return 0;
    }

    for (component = 0; component < params->components; ++component)
    {
        if (!dic_j2k_write_u8(file, 7u)
            || !dic_j2k_write_u8(file, 1u)
            || !dic_j2k_write_u8(file, 1u))
        {
            return 0;
        }
    }

    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.1, Figure A.9 and Tables A.12-A.20, Coding style default syntax. */
static int dic_j2k_write_cod(FILE *file, const dic_j2k_basic_params *params)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t scod = 0u;
    uint16_t length = (uint16_t)(12u + (params->use_precincts ? (uint16_t)params->decomposition_levels + 1u : 0u));
    uint8_t resolution;

    if (params->use_precincts)
        scod |= 0x01u;
    if (params->use_sop)
        scod |= 0x02u;
    if (params->use_eph)
        scod |= 0x04u;

    if (!dic_j2k_write_marker(file, DIC_J2K_MARKER_COD)
        || !dic_j2k_write_u16_be(file, length)
        || !dic_j2k_write_u8(file, scod)
        || !dic_j2k_write_u8(file, 0u)
        || !dic_j2k_write_u16_be(file, params->layers == 0u ? 1u : params->layers)
        || !dic_j2k_write_u8(file, params->multiple_component_transform ? 1u : 0u)
        || !dic_j2k_write_u8(file, params->decomposition_levels)
        || !dic_j2k_write_u8(file, 4u)
        || !dic_j2k_write_u8(file, 4u)
        || !dic_j2k_write_u8(file, 0x04u)
        || !dic_j2k_write_u8(file, params->reversible ? 1u : 0u))
    {
        return 0;
    }

    for (resolution = 0u; params->use_precincts && resolution <= params->decomposition_levels; ++resolution)
    {
        uint8_t precinct = (uint8_t)(
            (uint8_t)(params->precinct_height_exponents[resolution] << 4u)
            | params->precinct_width_exponents[resolution]
        );

        if (!dic_j2k_write_u8(file, precinct))
            return 0;
    }

    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.3 Tables A.24-A.26, RGN signals implicit Maxshift ROI scaling. */
static int dic_j2k_write_rgn(FILE *file, const dic_j2k_basic_params *params)
{
    DIC_J2K_DEBUG_ENTER();
    uint16_t component;

    if (params->roi_shift == 0u)
        return 1;

    for (component = 0u; component < params->components; ++component)
    {
        if (!dic_j2k_write_marker(file, DIC_J2K_MARKER_RGN)
            || !dic_j2k_write_u16_be(file, 5u)
            || !dic_j2k_write_u8(file, component)
            || !dic_j2k_write_u8(file, 0u)
            || !dic_j2k_write_u8(file, params->roi_shift))
        {
            return 0;
        }
    }

    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.4 Table A.29 and E.1.1 Table E.1, reversible SPqcd stores the exponent in the high five bits. */
static uint8_t dic_j2k_reversible_qcd_spqcd(unsigned int subband_in_level, uint8_t component_extra_bits)
{
    DIC_J2K_DEBUG_ENTER();
    static const uint8_t gain_log2_by_subband[] = {1u, 1u, 2u};
    uint8_t exponent;

    if (subband_in_level >= 3u)
        exponent = (uint8_t)(8u + component_extra_bits);
    else
        exponent = (uint8_t)(8u + component_extra_bits + gain_log2_by_subband[subband_in_level]);
    return (uint8_t)(exponent << 3u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.4 Tables A.27-A.29 and E.2 Equation E-10, reversible QCD syntax. */
static int dic_j2k_write_qcd(FILE *file, const dic_j2k_basic_params *params)
{
    DIC_J2K_DEBUG_ENTER();
    unsigned int level;
    uint16_t length = (uint16_t)(4u + 3u * (unsigned int)params->decomposition_levels);

    if (!dic_j2k_write_marker(file, DIC_J2K_MARKER_QCD)
        || !dic_j2k_write_u16_be(file, length)
        || !dic_j2k_write_u8(file, 0x40u)
        || !dic_j2k_write_u8(file, dic_j2k_reversible_qcd_spqcd(3u, 0u)))
    {
        return 0;
    }

    for (level = 0u; level < params->decomposition_levels; ++level)
    {
        if (!dic_j2k_write_u8(file, dic_j2k_reversible_qcd_spqcd(0u, 0u))
            || !dic_j2k_write_u8(file, dic_j2k_reversible_qcd_spqcd(1u, 0u))
            || !dic_j2k_write_u8(file, dic_j2k_reversible_qcd_spqcd(2u, 0u)))
        {
            return 0;
        }
    }

    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.5 and E.2 Equation E-10, RCT chroma components signal one additional bit of reversible precision with QCC. */
static int dic_j2k_write_qcc(
    FILE *file,
    const dic_j2k_basic_params *params,
    uint16_t component,
    uint8_t component_extra_bits
)
{
    DIC_J2K_DEBUG_ENTER();
    unsigned int level;
    uint16_t length = (uint16_t)(5u + 3u * (unsigned int)params->decomposition_levels);

    if (params->components > 256u)
        return 0;
    if (!dic_j2k_write_marker(file, DIC_J2K_MARKER_QCC)
        || !dic_j2k_write_u16_be(file, length)
        || !dic_j2k_write_u8(file, (uint8_t)component)
        || !dic_j2k_write_u8(file, 0x40u)
        || !dic_j2k_write_u8(file, dic_j2k_reversible_qcd_spqcd(3u, component_extra_bits)))
    {
        return 0;
    }

    for (level = 0u; level < params->decomposition_levels; ++level)
    {
        if (!dic_j2k_write_u8(file, dic_j2k_reversible_qcd_spqcd(0u, component_extra_bits))
            || !dic_j2k_write_u8(file, dic_j2k_reversible_qcd_spqcd(1u, component_extra_bits))
            || !dic_j2k_write_u8(file, dic_j2k_reversible_qcd_spqcd(2u, component_extra_bits)))
        {
            return 0;
        }
    }

    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.2 and Table A.5, Start of tile-part syntax and Psot. */
static int dic_j2k_write_sot(
    FILE *file,
    uint16_t tile_index,
    size_t payload_size,
    uint8_t tile_part_index,
    uint8_t tile_part_count
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t tile_part_length;

    if (payload_size > UINT32_MAX - 14u)
        return 0;

    tile_part_length = (uint32_t)payload_size + 14u;
    return dic_j2k_write_marker(file, DIC_J2K_MARKER_SOT)
        && dic_j2k_write_u16_be(file, 10u)
        && dic_j2k_write_u16_be(file, tile_index)
        && dic_j2k_write_u32_be(file, tile_part_length)
        && dic_j2k_write_u8(file, tile_part_index)
        && dic_j2k_write_u8(file, tile_part_count);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.6, main header marker segments precede every tile-part. */
static int dic_j2k_write_main_header(FILE *file, const dic_j2k_basic_params *params)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_write_marker(file, DIC_J2K_MARKER_SOC)
        && dic_j2k_write_siz(file, params)
        && dic_j2k_write_cod(file, params)
        && dic_j2k_write_qcd(file, params)
        && (params->multiple_component_transform
            ? (dic_j2k_write_qcc(file, params, 1u, 1u)
                && dic_j2k_write_qcc(file, params, 2u, 1u))
            : 1)
        && dic_j2k_write_rgn(file, params);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.2-A.4.3, a tile-part is SOT, SOD, then compressed data. */
static int dic_j2k_write_tile_part(FILE *file, const dic_j2k_tile_part_payload *tile_part)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_write_sot(
            file,
            tile_part->tile_index,
            tile_part->payload_size,
            tile_part->tile_part_index,
            tile_part->tile_part_count
        )
        && dic_j2k_write_marker(file, DIC_J2K_MARKER_SOD)
        && dic_j2k_write_bytes(file, tile_part->payload, tile_part->payload_size);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3 and Figures A.3-A.5, codestream and tile-part construction. */
dic_status dic_j2k_write_minimal_codestream(
    const char *path,
    const dic_j2k_basic_params *params
)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_write_empty_packet_codestream(path, params);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.2-A.4.4, SOT/Psot, SOD data, and EOC syntax. */
dic_status dic_j2k_write_codestream_with_payload(
    const char *path,
    const dic_j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_tile_part_payload tile_part;

    if (path == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (payload_size > 0u && payload == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    tile_part.tile_index = 0u;
    tile_part.tile_part_index = 0u;
    tile_part.tile_part_count = 1u;
    tile_part.payload = payload;
    tile_part.payload_size = payload_size;
    return dic_j2k_write_codestream_with_tile_parts(path, params, &tile_part, 1u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1, JP2 Contiguous Codestream box embeds a codestream byte stream. */
dic_status dic_j2k_write_minimal_codestream_stream(
    FILE *file,
    const dic_j2k_basic_params *params
)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_write_empty_packet_codestream_stream(file, params);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.4, main header followed by one tile-part and EOC. */
dic_status dic_j2k_write_codestream_with_payload_stream(
    FILE *file,
    const dic_j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_tile_part_payload tile_part;

    if (file == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (payload_size > 0u && payload == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    tile_part.tile_index = 0u;
    tile_part.tile_part_index = 0u;
    tile_part.tile_part_count = 1u;
    tile_part.payload = payload;
    tile_part.payload_size = payload_size;
    return dic_j2k_write_codestream_with_tile_parts_stream(file, params, &tile_part, 1u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.4, a codestream may contain multiple SOT/SOD tile-parts after the main header. */
dic_status dic_j2k_write_codestream_with_tile_parts(
    const char *path,
    const dic_j2k_basic_params *params,
    const dic_j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
)
{
    DIC_J2K_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status;

    if (path == NULL || params == NULL || tile_parts == NULL || tile_part_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = dic_j2k_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    status = dic_j2k_write_codestream_with_tile_parts_stream(file, params, tile_parts, tile_part_count);
    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.2 Table A.5, each SOT carries its tile index, tile-part index, and tile-part count. */
dic_status dic_j2k_write_codestream_with_tile_parts_stream(
    FILE *file,
    const dic_j2k_basic_params *params,
    const dic_j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t tiles_x;
    uint32_t tiles_y;
    uint32_t expected_tiles;
    size_t index;
    uint8_t *seen_tiles = NULL;
    dic_status status;

    if (file == NULL || params == NULL || tile_parts == NULL || tile_part_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (params->width == 0u || params->height == 0u)
        return DIC_J2K_INVALID_DIMENSIONS;
    if (params->components != 1u && params->components != 3u)
        return DIC_J2K_INVALID_COMPONENTS;
    if (params->multiple_component_transform && params->components != 3u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if ((params->tile_width != 0u && params->tile_width > params->width)
        || (params->tile_height != 0u && params->tile_height > params->height))
    {
        return DIC_STATUS_INVALID_ARGUMENT;
    }
    if (params->decomposition_levels > DIC_J2K_MAX_DECOMPOSITION_LEVELS)
        return DIC_J2K_INVALID_LEVELS;
    {
        dic_status precinct_status = dic_j2k_validate_precincts(params);

        if (precinct_status != DIC_STATUS_OK)
            return precinct_status;
    }
    status = dic_j2k_tile_grid(params, &tiles_x, &tiles_y, &expected_tiles);
    if (status != DIC_STATUS_OK)
        return status;
    (void)tiles_x;
    (void)tiles_y;

    if (tile_part_count < expected_tiles)
        return DIC_STATUS_INVALID_ARGUMENT;
    seen_tiles = (uint8_t *)calloc(expected_tiles, sizeof(seen_tiles[0]));
    if (seen_tiles == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    for (index = 0u; index < tile_part_count; ++index)
    {
        if (tile_parts[index].tile_index >= expected_tiles)
        {
            free(seen_tiles);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
        if (tile_parts[index].tile_part_count != 0u
            && tile_parts[index].tile_part_index >= tile_parts[index].tile_part_count)
        {
            free(seen_tiles);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
        if (tile_parts[index].payload_size > 0u && tile_parts[index].payload == NULL)
        {
            free(seen_tiles);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
        if (tile_parts[index].payload_size > UINT32_MAX - 14u)
        {
            free(seen_tiles);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
        if (tile_parts[index].tile_part_index == 0u)
            seen_tiles[tile_parts[index].tile_index] = 1u;
    }
    for (index = 0u; index < expected_tiles; ++index)
    {
        if (!seen_tiles[index])
        {
            free(seen_tiles);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
    }
    free(seen_tiles);

    if (!dic_j2k_write_main_header(file, params))
    {
        return DIC_STATUS_IO_ERROR;
    }

    for (index = 0u; index < tile_part_count; ++index)
    {
        if (!dic_j2k_write_tile_part(file, tile_parts + index))
            return DIC_STATUS_IO_ERROR;
    }

    if (!dic_j2k_write_marker(file, DIC_J2K_MARKER_EOC))
        return DIC_STATUS_IO_ERROR;

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.3 and B.10.8, empty packets still appear in LRCP packet order. */
dic_status dic_j2k_write_empty_packet_codestream(
    const char *path,
    const dic_j2k_basic_params *params
)
{
    DIC_J2K_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status;

    if (path == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = dic_j2k_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    status = dic_j2k_write_empty_packet_codestream_stream(file, params);
    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.1 and B.10.3, an empty packet is encoded by a zero first header bit padded to a byte. */
dic_status dic_j2k_write_empty_packet_codestream_stream(
    FILE *file,
    const dic_j2k_basic_params *params
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_packet_header payload;
    dic_j2k_tile_part_payload *tile_parts = NULL;
    uint32_t tiles_x;
    uint32_t tiles_y;
    uint32_t tile_count;
    uint32_t tile_index;
    dic_status status;

    if (file == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_j2k_packet_header_init(&payload);
    status = dic_j2k_tile_grid(params, &tiles_x, &tiles_y, &tile_count);
    if (status != DIC_STATUS_OK)
    {
        dic_j2k_packet_header_free(&payload);
        return status;
    }
    (void)tiles_x;
    (void)tiles_y;

    status = dic_j2k_packet_build_empty_lrcp_payload(
        params->components,
        params->decomposition_levels,
        params->layers,
        &payload
    );
    if (status == DIC_STATUS_OK)
    {
        tile_parts = (dic_j2k_tile_part_payload *)calloc(tile_count, sizeof(tile_parts[0]));
        if (tile_parts == NULL)
        {
            status = DIC_STATUS_MEMORY_ERROR;
        }
    }
    for (tile_index = 0u; status == DIC_STATUS_OK && tile_index < tile_count; ++tile_index)
    {
        tile_parts[tile_index].tile_index = (uint16_t)tile_index;
        tile_parts[tile_index].tile_part_index = 0u;
        tile_parts[tile_index].tile_part_count = 1u;
        tile_parts[tile_index].payload = payload.data;
        tile_parts[tile_index].payload_size = payload.size;
    }
    if (status == DIC_STATUS_OK)
    {
        status = dic_j2k_write_codestream_with_tile_parts_stream(file, params, tile_parts, tile_count);
    }
    free(tile_parts);
    dic_j2k_packet_header_free(&payload);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.3 and B.10.8, SOD carries packet headers followed by EBCOT code-block bytes. */
dic_status dic_j2k_write_ebcot_packet_codestream(
    const char *path,
    const dic_j2k_basic_params *params,
    const dic_j2k_codeblock_stream *streams,
    size_t stream_count
)
{
    DIC_J2K_DEBUG_ENTER();
    FILE *file;
    dic_status status;

    if (path == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = dic_j2k_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;

    status = dic_j2k_write_ebcot_packet_codestream_stream(file, params, streams, stream_count);
    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.8, packet payloads are generated in code-block scan order for the current packet. */
dic_status dic_j2k_write_ebcot_packet_codestream_stream(
    FILE *file,
    const dic_j2k_basic_params *params,
    const dic_j2k_codeblock_stream *streams,
    size_t stream_count
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_packet_header payload;
    dic_status status;

    if (file == NULL || params == NULL || (streams == NULL && stream_count > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_j2k_packet_header_init(&payload);
    status = dic_j2k_packet_build_ebcot_payload(streams, stream_count, &payload);
    if (status == DIC_STATUS_OK)
    {
        status = dic_j2k_write_codestream_with_payload_stream(
            file,
            params,
            payload.data,
            payload.size
        );
    }

    dic_j2k_packet_header_free(&payload);
    return status;
}

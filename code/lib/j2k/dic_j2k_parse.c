/**
 * @file dic_j2k_parse.c
 * @brief Parses the JPEG 2000 codestream and JP2 wrapper syntax used by T.800 Annex A and Annex I.
 *
 * The parser extracts SIZ/COD/RGN/SOT information and counts tile-part payload bytes for
 * project validation. It is intentionally metadata-oriented and does not decode packet
 * bodies or all optional marker segments; unsupported marker segments are skipped using
 * their Annex A length fields.
 *
 * References: dic_j2k_codestream.h for parsed parameters, dic_jp2_file.c for matching JP2
 * box emission, and Annex J examples for stream structure and packet payload interpretation.
 */

#include "j2k/dic_j2k_parse.h"
#include "j2k/dic_j2k_debug.h"

#include "j2k/dic_jp2_file.h"

#include <stdio.h>
#include <string.h>

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A marker segments and Annex I JP2 boxes use big-endian integer fields. */
static int dic_j2k_read_u8(FILE *file, uint8_t *value)
{
    DIC_J2K_DEBUG_ENTER();
    int byte = fgetc(file);

    if (byte == EOF)
        return 0;
    *value = (uint8_t)byte;
    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A, marker codes and segment lengths are 16-bit fields. */
static int dic_j2k_read_u16_be(FILE *file, uint16_t *value)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t hi;
    uint8_t lo;

    if (!dic_j2k_read_u8(file, &hi) || !dic_j2k_read_u8(file, &lo))
        return 0;
    *value = (uint16_t)(((uint16_t)hi << 8) | lo);
    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Tables A.5 and A.9, geometry and tile-part length fields are 32-bit. */
static int dic_j2k_read_u32_be(FILE *file, uint32_t *value)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
    uint8_t b3;

    if (!dic_j2k_read_u8(file, &b0) || !dic_j2k_read_u8(file, &b1)
        || !dic_j2k_read_u8(file, &b2) || !dic_j2k_read_u8(file, &b3))
    {
        return 0;
    }
    *value = ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | b3;
    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.5.1 Table A.9, SIZ carries image size and component count. */
static dic_status dic_j2k_parse_siz(FILE *file, uint16_t length, dic_j2k_codestream_info *info)
{
    DIC_J2K_DEBUG_ENTER();
    uint16_t rsiz;
    uint32_t ignore;
    uint16_t components;
    uint16_t component;

    if (length < 41u)
        return DIC_J2K_FORMAT_ERROR;
    if (!dic_j2k_read_u16_be(file, &rsiz)
        || !dic_j2k_read_u32_be(file, &info->params.width)
        || !dic_j2k_read_u32_be(file, &info->params.height)
        || !dic_j2k_read_u32_be(file, &ignore)
        || !dic_j2k_read_u32_be(file, &ignore)
        || !dic_j2k_read_u32_be(file, &info->params.tile_width)
        || !dic_j2k_read_u32_be(file, &info->params.tile_height)
        || !dic_j2k_read_u32_be(file, &ignore)
        || !dic_j2k_read_u32_be(file, &ignore)
        || !dic_j2k_read_u16_be(file, &components))
    {
        return DIC_STATUS_FILE_READ_ERROR;
    }
    (void)rsiz;
    if (info->params.tile_width == info->params.width)
        info->params.tile_width = 0u;
    if (info->params.tile_height == info->params.height)
        info->params.tile_height = 0u;
    info->params.components = components;

    for (component = 0u; component < components; ++component)
    {
        uint8_t ssiz;
        uint8_t xrsiz;
        uint8_t yrsiz;

        if (!dic_j2k_read_u8(file, &ssiz)
            || !dic_j2k_read_u8(file, &xrsiz)
            || !dic_j2k_read_u8(file, &yrsiz))
        {
            return DIC_STATUS_FILE_READ_ERROR;
        }
    }

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.1 Figure A.9, COD carries MCT, decomposition levels, and transform. */
static dic_status dic_j2k_parse_cod(FILE *file, uint16_t length, dic_j2k_codestream_info *info)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t scod;
    uint8_t progression;
    uint16_t layers;
    uint8_t mct;
    uint8_t levels;
    uint8_t ignored;
    uint8_t transform;

    if (length < 12u)
        return DIC_J2K_FORMAT_ERROR;
    if (!dic_j2k_read_u8(file, &scod)
        || !dic_j2k_read_u8(file, &progression)
        || !dic_j2k_read_u16_be(file, &layers)
        || !dic_j2k_read_u8(file, &mct)
        || !dic_j2k_read_u8(file, &levels)
        || !dic_j2k_read_u8(file, &ignored)
        || !dic_j2k_read_u8(file, &ignored)
        || !dic_j2k_read_u8(file, &ignored)
        || !dic_j2k_read_u8(file, &transform))
    {
        return DIC_STATUS_FILE_READ_ERROR;
    }
    (void)scod;
    (void)progression;
    info->params.layers = layers;
    info->params.multiple_component_transform = mct;
    info->params.decomposition_levels = levels;
    info->params.reversible = transform == 1u ? 1u : 0u;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.3 Tables A.24-A.26, RGN carries the implicit Maxshift value. */
static dic_status dic_j2k_parse_rgn(FILE *file, uint16_t length, dic_j2k_codestream_info *info)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t crgn;
    uint8_t srgn;
    uint8_t sprgn;

    if (length != 5u)
        return DIC_J2K_FORMAT_ERROR;
    if (!dic_j2k_read_u8(file, &crgn)
        || !dic_j2k_read_u8(file, &srgn)
        || !dic_j2k_read_u8(file, &sprgn))
    {
        return DIC_STATUS_FILE_READ_ERROR;
    }
    (void)crgn;
    if (srgn != 0u)
        return DIC_J2K_FORMAT_ERROR;
    info->params.roi_shift = sprgn;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.2 Table A.5, SOT Psot counts bytes from SOT marker through tile-part data. */
static dic_status dic_j2k_parse_sot(FILE *file, uint16_t length, dic_j2k_codestream_info *info)
{
    DIC_J2K_DEBUG_ENTER();
    uint16_t isot;
    uint32_t psot;
    uint8_t tpsot;
    uint8_t tnsot;

    if (length != 10u)
        return DIC_J2K_FORMAT_ERROR;
    if (!dic_j2k_read_u16_be(file, &isot)
        || !dic_j2k_read_u32_be(file, &psot)
        || !dic_j2k_read_u8(file, &tpsot)
        || !dic_j2k_read_u8(file, &tnsot))
    {
        return DIC_STATUS_FILE_READ_ERROR;
    }
    (void)isot;
    (void)tpsot;
    (void)tnsot;
    info->tile_part_payload_bytes = psot >= 14u ? (size_t)psot - 14u : 0u;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.4, codestream parsing follows SOC, marker segments, SOD payload, and EOC. */
static dic_status dic_j2k_read_codestream_info_stream(FILE *file, dic_j2k_codestream_info *info)
{
    DIC_J2K_DEBUG_ENTER();
    uint16_t marker;

    memset(info, 0, sizeof(*info));
    if (!dic_j2k_read_u16_be(file, &marker) || marker != DIC_J2K_MARKER_SOC)
        return DIC_J2K_FORMAT_ERROR;

    while (dic_j2k_read_u16_be(file, &marker))
    {
        uint16_t length;
        dic_status status = DIC_STATUS_OK;

        if (marker == DIC_J2K_MARKER_EOC)
            return DIC_STATUS_OK;
        if (marker == DIC_J2K_MARKER_SOD)
        {
            if (info->tile_part_payload_bytes > 0u
                && fseek(file, (long)info->tile_part_payload_bytes, SEEK_CUR) != 0)
            {
                return DIC_STATUS_FILE_READ_ERROR;
            }
            continue;
        }

        if (!dic_j2k_read_u16_be(file, &length) || length < 2u)
            return DIC_STATUS_FILE_READ_ERROR;

        if (marker == DIC_J2K_MARKER_SIZ)
            status = dic_j2k_parse_siz(file, length, info);
        else if (marker == DIC_J2K_MARKER_COD)
            status = dic_j2k_parse_cod(file, length, info);
        else if (marker == DIC_J2K_MARKER_RGN)
            status = dic_j2k_parse_rgn(file, length, info);
        else if (marker == DIC_J2K_MARKER_SOT)
            status = dic_j2k_parse_sot(file, length, info);
        else if (fseek(file, (long)length - 2L, SEEK_CUR) != 0)
            status = DIC_STATUS_FILE_READ_ERROR;

        if (status != DIC_STATUS_OK)
            return status;
    }

    return DIC_J2K_FORMAT_ERROR;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3, a raw codestream starts with SOC. */
dic_status dic_j2k_read_codestream_info(
    const char *path,
    dic_j2k_codestream_info *info
)
{
    DIC_J2K_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status;

    if (path == NULL || info == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0)
        return DIC_STATUS_FILE_OPEN_ERROR;
#else
    file = fopen(path, "rb");
    if (file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;
#endif
    status = dic_j2k_read_codestream_info_stream(file, info);
    fclose(file);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1, JP2 parsing locates the Contiguous Codestream box. */
dic_status dic_jp2_read_codestream_info(
    const char *path,
    dic_j2k_codestream_info *info
)
{
    DIC_J2K_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status = DIC_J2K_FORMAT_ERROR;

    if (path == NULL || info == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0)
        return DIC_STATUS_FILE_OPEN_ERROR;
#else
    file = fopen(path, "rb");
    if (file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;
#endif

    while (!feof(file))
    {
        uint32_t length;
        uint32_t type;
        long payload_start;

        if (!dic_j2k_read_u32_be(file, &length) || !dic_j2k_read_u32_be(file, &type))
            break;

        payload_start = ftell(file);
        if (type == DIC_JP2_BOX_JP2C)
        {
            status = dic_j2k_read_codestream_info_stream(file, info);
            break;
        }
        if (length < 8u || fseek(file, payload_start + (long)length - 8L, SEEK_SET) != 0)
            break;
    }

    fclose(file);
    return status;
}

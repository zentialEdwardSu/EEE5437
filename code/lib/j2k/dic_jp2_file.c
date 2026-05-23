/**
 * @file dic_jp2_file.c
 * @brief Writes the optional JP2 file-format boxes specified by T.800 Annex I.
 *
 * The file emits the JP signature, file type, JP2 header, image header, colour specification,
 * and contiguous codestream boxes around the constrained codestream writer. It targets the
 * minimal JP2 profile needed by this project and does not emit optional resolution, channel
 * definition, palette, XML, UUID, or intellectual-property boxes.
 *
 * References: dic_jp2_file.h for box identifiers, dic_j2k_codestream.c for Annex A codestream
 * payloads, and Annex J.15 for colourspace/YCC handling guidance.
 */

#include "j2k/dic_jp2_file.h"
#include "j2k/dic_j2k_debug.h"

#include <stdio.h>

enum
{
    DIC_JP2_SIGNATURE = 0x0d0a870au,
    DIC_JP2_BRAND_JP2 = 0x6a703220u,
    DIC_JP2_ENUMERATED_SRGB = 16,
    DIC_JP2_ENUMERATED_GREYSCALE = 17
};

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.4-I.5, JP2 file format and box structure. */
static FILE *dic_jp2_open_file(const char *path, const char *mode)
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

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.4, JP2 box fields are serialized in file byte order. */
static int dic_jp2_write_u8(FILE *file, unsigned int value)
{
    DIC_J2K_DEBUG_ENTER();
    return fputc((int)(value & 0xffu), file) != EOF;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.3.1 Table I.5, Image Header box fields. */
static int dic_jp2_write_u16_be(FILE *file, unsigned int value)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_jp2_write_u8(file, value >> 8)
        && dic_jp2_write_u8(file, value);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.4, JP2 boxes start with LBox and TBox. */
static int dic_jp2_write_u32_be(FILE *file, unsigned int value)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_jp2_write_u8(file, value >> 24)
        && dic_jp2_write_u8(file, value >> 16)
        && dic_jp2_write_u8(file, value >> 8)
        && dic_jp2_write_u8(file, value);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.4, JP2 LBox/TBox box header syntax. */
static int dic_jp2_write_box_header(FILE *file, unsigned int length, unsigned int type)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_jp2_write_u32_be(file, length)
        && dic_jp2_write_u32_be(file, type);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.1, JPEG 2000 Signature box. */
static int dic_jp2_write_signature_box(FILE *file)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_jp2_write_box_header(file, 12u, DIC_JP2_BOX_JP)
        && dic_jp2_write_u32_be(file, DIC_JP2_SIGNATURE);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2, File Type box brand and compatibility list. */
static int dic_jp2_write_file_type_box(FILE *file)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_jp2_write_box_header(file, 20u, DIC_JP2_BOX_FTYP)
        && dic_jp2_write_u32_be(file, DIC_JP2_BRAND_JP2)
        && dic_jp2_write_u32_be(file, 0u)
        && dic_jp2_write_u32_be(file, DIC_JP2_BRAND_JP2);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.3.1, Image Header box. */
static int dic_jp2_write_image_header_box(FILE *file, const dic_j2k_basic_params *params)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_jp2_write_box_header(file, 22u, DIC_JP2_BOX_IHDR)
        && dic_jp2_write_u32_be(file, params->height)
        && dic_jp2_write_u32_be(file, params->width)
        && dic_jp2_write_u16_be(file, params->components)
        && dic_jp2_write_u8(file, 7u)
        && dic_jp2_write_u8(file, 7u)
        && dic_jp2_write_u8(file, 0u)
        && dic_jp2_write_u8(file, 0u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.3.3, Colour Specification box. */
static int dic_jp2_write_colour_specification_box(FILE *file, const dic_j2k_basic_params *params)
{
    DIC_J2K_DEBUG_ENTER();
    unsigned int enum_colourspace = params->components == 1u
        ? DIC_JP2_ENUMERATED_GREYSCALE
        : DIC_JP2_ENUMERATED_SRGB;

    return dic_jp2_write_box_header(file, 15u, DIC_JP2_BOX_COLR)
        && dic_jp2_write_u8(file, 1u)
        && dic_jp2_write_u8(file, 0u)
        && dic_jp2_write_u8(file, 0u)
        && dic_jp2_write_u32_be(file, enum_colourspace);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.3, JP2 Header superbox. */
static int dic_jp2_write_header_box(FILE *file, const dic_j2k_basic_params *params)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_jp2_write_box_header(file, 45u, DIC_JP2_BOX_JP2H)
        && dic_jp2_write_image_header_box(file, params)
        && dic_jp2_write_colour_specification_box(file, params);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1, Contiguous Codestream box. */
static int dic_jp2_write_contiguous_codestream_box(FILE *file, const dic_j2k_basic_params *params)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_jp2_write_box_header(file, 0u, DIC_JP2_BOX_JP2C)
        && dic_j2k_write_empty_packet_codestream_stream(file, params) == DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1, the Contiguous Codestream box may contain a complete codestream with tile-part payload. */
static int dic_jp2_write_contiguous_codestream_payload_box(
    FILE *file,
    const dic_j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_jp2_write_box_header(file, 0u, DIC_JP2_BOX_JP2C)
        && dic_j2k_write_codestream_with_payload_stream(file, params, payload, payload_size) == DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1, the Contiguous Codestream box may contain a complete multi-tile codestream. */
static int dic_jp2_write_contiguous_codestream_tile_parts_box(
    FILE *file,
    const dic_j2k_basic_params *params,
    const dic_j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_jp2_write_box_header(file, 0u, DIC_JP2_BOX_JP2C)
        && dic_j2k_write_codestream_with_tile_parts_stream(file, params, tile_parts, tile_part_count) == DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5, required JP2 top-level boxes and ordering. */
dic_status dic_jp2_write_minimal_file(
    const char *path,
    const dic_j2k_basic_params *params
)
{
    DIC_J2K_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status = DIC_STATUS_OK;

    if (path == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (params->width == 0u || params->height == 0u)
        return DIC_J2K_INVALID_DIMENSIONS;
    if (params->components != 1u && params->components != 3u)
        return DIC_J2K_INVALID_COMPONENTS;

    file = dic_jp2_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (!dic_jp2_write_signature_box(file)
        || !dic_jp2_write_file_type_box(file)
        || !dic_jp2_write_header_box(file, params)
        || !dic_jp2_write_contiguous_codestream_box(file, params))
    {
        status = DIC_STATUS_IO_ERROR;
    }

    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5, JP2 required boxes followed by a payload-bearing contiguous codestream. */
dic_status dic_jp2_write_file_with_codestream_payload(
    const char *path,
    const dic_j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
)
{
    DIC_J2K_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status = DIC_STATUS_OK;

    if (path == NULL || params == NULL || (payload == NULL && payload_size > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;
    if (params->width == 0u || params->height == 0u)
        return DIC_J2K_INVALID_DIMENSIONS;
    if (params->components != 1u && params->components != 3u)
        return DIC_J2K_INVALID_COMPONENTS;

    file = dic_jp2_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (!dic_jp2_write_signature_box(file)
        || !dic_jp2_write_file_type_box(file)
        || !dic_jp2_write_header_box(file, params)
        || !dic_jp2_write_contiguous_codestream_payload_box(file, params, payload, payload_size))
    {
        status = DIC_STATUS_IO_ERROR;
    }

    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5, JP2 boxes wrap the same complete codestream used for raw J2K output. */
dic_status dic_jp2_write_file_with_codestream_tile_parts(
    const char *path,
    const dic_j2k_basic_params *params,
    const dic_j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
)
{
    DIC_J2K_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status = DIC_STATUS_OK;

    if (path == NULL || params == NULL || tile_parts == NULL || tile_part_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (params->width == 0u || params->height == 0u)
        return DIC_J2K_INVALID_DIMENSIONS;
    if (params->components != 1u && params->components != 3u)
        return DIC_J2K_INVALID_COMPONENTS;

    file = dic_jp2_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (!dic_jp2_write_signature_box(file)
        || !dic_jp2_write_file_type_box(file)
        || !dic_jp2_write_header_box(file, params)
        || !dic_jp2_write_contiguous_codestream_tile_parts_box(file, params, tile_parts, tile_part_count))
    {
        status = DIC_STATUS_IO_ERROR;
    }

    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

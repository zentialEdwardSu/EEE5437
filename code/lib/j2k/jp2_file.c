/**
 * @file jp2_file.c
 * @brief Writes the optional JP2 file-format boxes specified by T.800 Annex I.
 *
 * The file emits the JP signature, file type, JP2 header, image header, colour specification,
 * and contiguous codestream boxes around the constrained codestream writer. It targets the
 * minimal JP2 profile needed by this project and does not emit optional resolution, channel
 * definition, palette, XML, UUID, or intellectual-property boxes.
 *
 * References: jp2_file.h for box identifiers, j2k_codestream.c for Annex A codestream
 * payloads, and Annex J.15 for colourspace/YCC handling guidance.
 */

#include "j2k/jp2_file.h"
#include "j2k/j2k_debug.h"
#include "fs/fs.h"
#include <stdio.h>

enum
{
    jp2_SIGNATURE = 0x0d0a870au,
    jp2_BRAND_JP2 = 0x6a703220u,
    jp2_ENUMERATED_SRGB = 16,
    jp2_ENUMERATED_GREYSCALE = 17
};

/* u8 writer*/
static int jp2_write_u8(FILE *file, unsigned int value)
{
    return fputc((int)(value & 0xffu), file) != EOF;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.3.1 Table I.5, Image Header box fields. */
static int jp2_write_u16_be(FILE *file, unsigned int value)
{
    j2k_DEBUG_ENTER();
    return jp2_write_u8(file, value >> 8)
        && jp2_write_u8(file, value);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.4, JP2 boxes start with LBox and TBox. */
static int jp2_write_u32_be(FILE *file, unsigned int value)
{
    j2k_DEBUG_ENTER();
    return jp2_write_u8(file, value >> 24)
        && jp2_write_u8(file, value >> 16)
        && jp2_write_u8(file, value >> 8)
        && jp2_write_u8(file, value);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.4, JP2 LBox/TBox box header syntax. */
static int jp2_write_box_header(FILE *file, unsigned int length, unsigned int type)
{
    j2k_DEBUG_ENTER();
    return jp2_write_u32_be(file, length)
        && jp2_write_u32_be(file, type);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.1, JPEG 2000 Signature box. */
static int jp2_write_signature_box(FILE *file)
{
    j2k_DEBUG_ENTER();
    return jp2_write_box_header(file, 12u, jp2_BOX_JP)
        && jp2_write_u32_be(file, jp2_SIGNATURE);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2, File Type box brand and compatibility list. */
static int jp2_write_file_type_box(FILE *file)
{
    j2k_DEBUG_ENTER();
    return jp2_write_box_header(file, 20u, jp2_BOX_FTYP)
        && jp2_write_u32_be(file, jp2_BRAND_JP2)
        && jp2_write_u32_be(file, 0u)
        && jp2_write_u32_be(file, jp2_BRAND_JP2);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.3.1, Image Header box. */
static int jp2_write_image_header_box(FILE *file, const j2k_basic_params *params)
{
    j2k_DEBUG_ENTER();
    return jp2_write_box_header(file, 22u, jp2_BOX_IHDR)
        && jp2_write_u32_be(file, params->height)
        && jp2_write_u32_be(file, params->width)
        && jp2_write_u16_be(file, params->components)
        && jp2_write_u8(file, 7u)
        && jp2_write_u8(file, 7u)
        && jp2_write_u8(file, 0u)
        && jp2_write_u8(file, 0u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.3.3, Colour Specification box. */
static int jp2_write_colour_specification_box(FILE *file, const j2k_basic_params *params)
{
    j2k_DEBUG_ENTER();
    unsigned int enum_colourspace = params->components == 1u
        ? jp2_ENUMERATED_GREYSCALE
        : jp2_ENUMERATED_SRGB;

    return jp2_write_box_header(file, 15u, jp2_BOX_COLR)
        && jp2_write_u8(file, 1u)
        && jp2_write_u8(file, 0u)
        && jp2_write_u8(file, 0u)
        && jp2_write_u32_be(file, enum_colourspace);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.3, JP2 Header superbox. */
static int jp2_write_header_box(FILE *file, const j2k_basic_params *params)
{
    j2k_DEBUG_ENTER();
    return jp2_write_box_header(file, 45u, jp2_BOX_JP2H)
        && jp2_write_image_header_box(file, params)
        && jp2_write_colour_specification_box(file, params);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5, required JP2 top-level boxes and ordering. */
dic_status jp2_write_minimal_file(
    const char *path,
    const j2k_basic_params *params
)
{
    j2k_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status = DIC_STATUS_OK;

    if (path == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (params->width == 0u || params->height == 0u)
        return DIC_J2K_INVALID_DIMENSIONS;
    if (params->components != 1u && params->components != 3u)
        return DIC_J2K_INVALID_COMPONENTS;

    file = fs_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (!jp2_write_signature_box(file)
        || !jp2_write_file_type_box(file)
        || !jp2_write_header_box(file, params)
        || !jp2_write_box_header(file, 0u, jp2_BOX_JP2C)
        || j2k_write_empty_packet_codestream_stream(file, params) != DIC_STATUS_OK)
    {
        status = DIC_STATUS_IO_ERROR;
    }

    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5, JP2 required boxes followed by a payload-bearing contiguous codestream. */
dic_status jp2_write_file_with_codestream_payload(
    const char *path,
    const j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
)
{
    j2k_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status = DIC_STATUS_OK;

    if (path == NULL || params == NULL || (payload == NULL && payload_size > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;
    if (params->width == 0u || params->height == 0u)
        return DIC_J2K_INVALID_DIMENSIONS;
    if (params->components != 1u && params->components != 3u)
        return DIC_J2K_INVALID_COMPONENTS;

    file = fs_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (!jp2_write_signature_box(file)
        || !jp2_write_file_type_box(file)
        || !jp2_write_header_box(file, params)
        || !jp2_write_box_header(file, 0u, jp2_BOX_JP2C)
        || j2k_write_codestream_with_payload_stream(file, params, payload, payload_size) != DIC_STATUS_OK)
    {
        status = DIC_STATUS_IO_ERROR;
    }

    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5, JP2 boxes wrap the same complete codestream used for raw J2K output. */
dic_status jp2_write_file_with_codestream_tile_parts(
    const char *path,
    const j2k_basic_params *params,
    const j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
)
{
    j2k_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status = DIC_STATUS_OK;

    if (path == NULL || params == NULL || tile_parts == NULL || tile_part_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (params->width == 0u || params->height == 0u)
        return DIC_J2K_INVALID_DIMENSIONS;
    if (params->components != 1u && params->components != 3u)
        return DIC_J2K_INVALID_COMPONENTS;

    file = fs_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (!jp2_write_signature_box(file)
        || !jp2_write_file_type_box(file)
        || !jp2_write_header_box(file, params)
        || !jp2_write_box_header(file, 0u, jp2_BOX_JP2C)
        || j2k_write_codestream_with_tile_parts_stream(file, params, tile_parts, tile_part_count) != DIC_STATUS_OK)
    {
        status = DIC_STATUS_IO_ERROR;
    }

    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

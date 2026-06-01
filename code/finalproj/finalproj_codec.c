/**
 * @file finalproj_codec.c
 * @brief Assignment-facing codec API plus JPEG 2000 demo helpers.
 *
 * Impl imageEncoder writes image.bit with the basic 5/3-DWT, quantization, prediction, scan/EZT, and
 * Huffman path; imageDecoder reconstructs image_recon.pgm or image_recon.ppm and
 * reports PSNR. 
 */

#include "finalproj/finalproj_codec.h"

#include <stdint.h>
#include <stdio.h>

#include "codec/dic_basic_codec.h"
#include "codec/dic_basic_file.h"
#include "codec/dic_metrics.h"
#include "image_u8/image_u8.h"
#include "j2k/j2k_codestream.h"
#include "j2k/j2k_image.h"
#include "j2k/j2k_parse.h"
#include "j2k/jp2_file.h"
#include "ppm/ppm.h"
#include "fs/fs.h"

static long finalproj_file_size_bytes(const char *path)
{
    FILE *file = NULL;
    long size = -1;

    file = fs_open_file(path, "rb");
    if (file == NULL)
        return -1;

    if (fseek(file, 0, SEEK_END) == 0)
        size = ftell(file);
    fclose(file);
    return size;
}

double imageEncoder(const char *orgImageFileName, int quantizationStepSize)
{
    dic_image_u8 original = {0};
    dic_basic_encoded_image encoded = {0};
    long bitstream_size;
    double bitrate = -1.0;

    if (orgImageFileName == NULL || quantizationStepSize <= 0)
        return -1.0;

    if (dic_ppm_read(orgImageFileName, &original) != DIC_STATUS_OK)
        return -1.0;

    if (dic_basic_encode_image(
            original.data,
            original.width,
            original.height,
            original.channels,
            FINALPROJ_LEVELS,
            quantizationStepSize,
            &encoded) != DIC_STATUS_OK)
    {
        dic_image_u8_free(&original);
        return -1.0;
    }

    if (dic_basic_write_file(FINALPROJ_BITSTREAM_PATH, &encoded) == DIC_STATUS_OK)
    {
        bitstream_size = finalproj_file_size_bytes(FINALPROJ_BITSTREAM_PATH);
        if (bitstream_size >= 0)
            bitrate = dic_metric_bitrate((size_t)bitstream_size * 8u, original.width, original.height);
    }

    dic_basic_encoded_free(&encoded);
    dic_image_u8_free(&original);
    return bitrate;
}

double imageDecoder(
    const char *bitstreamFileName,
    int quantizationStepSize,
    const char *orgImageFileName
)
{
    dic_basic_encoded_image encoded = {0};
    dic_image_u8 original = {0};
    dic_image_u8 decoded = {0};
    const char *reconstruction_path;
    double psnr = -1.0;
    size_t sample_count;

    if (bitstreamFileName == NULL || orgImageFileName == NULL || quantizationStepSize <= 0)
        return -1.0;

    if (dic_basic_read_file(bitstreamFileName, &encoded) != DIC_STATUS_OK)
        return -1.0;
    if (encoded.quant_step != quantizationStepSize)
    {
        dic_basic_encoded_free(&encoded);
        return -1.0;
    }

    if (dic_basic_decode_image(&encoded, &decoded) != DIC_STATUS_OK)
    {
        dic_basic_encoded_free(&encoded);
        return -1.0;
    }

    if (dic_ppm_read(orgImageFileName, &original) == DIC_STATUS_OK
        && original.width == decoded.width
        && original.height == decoded.height
        && original.channels == decoded.channels)
    {
        sample_count = dic_image_u8_sample_count(original.width, original.height, original.channels);
        psnr = dic_metric_psnr_u8(original.data, decoded.data, sample_count);
    }

    reconstruction_path = decoded.channels == 1
        ? FINALPROJ_RECON_GRAY_PATH
        : FINALPROJ_RECON_RGB_PATH;
    if (dic_ppm_write(reconstruction_path, &decoded) != DIC_STATUS_OK)
        psnr = -1.0;

    dic_image_u8_free(&original);
    dic_image_u8_free(&decoded);
    dic_basic_encoded_free(&encoded);
    return psnr;
}

int imageWriteJ2KStub(
    const char *orgImageFileName,
    const char *outputFileName
)
{
    dic_image_u8 image = {0};
    j2k_basic_params params = {0};
    dic_status status;

    if (orgImageFileName == NULL || outputFileName == NULL)
        return 0;

    status = dic_ppm_read(orgImageFileName, &image);
    if (status != DIC_STATUS_OK)
        return 0;

    params.width = (uint32_t)image.width;
    params.height = (uint32_t)image.height;
    params.components = (uint16_t)image.channels;
    params.decomposition_levels = FINALPROJ_LEVELS;
    params.reversible = 1u;
    params.multiple_component_transform = image.channels == 3 ? 1u : 0u;

    status = j2k_write_empty_packet_codestream(outputFileName, &params);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

int imageWriteJ2K(
    const char *orgImageFileName,
    const char *outputFileName,
    int quality
)
{
    dic_image_u8 image = {0};
    dic_status status;

    if (orgImageFileName == NULL || outputFileName == NULL)
        return 0;

    status = dic_ppm_read(orgImageFileName, &image);
    if (status != DIC_STATUS_OK)
        return 0;

    status = j2k_write_image_codestream(outputFileName, &image, FINALPROJ_LEVELS, quality);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

int imageWriteJP2Stub(
    const char *orgImageFileName,
    const char *outputFileName
)
{
    dic_image_u8 image = {0};
    j2k_basic_params params = {0};
    dic_status status;

    if (orgImageFileName == NULL || outputFileName == NULL)
        return 0;

    status = dic_ppm_read(orgImageFileName, &image);
    if (status != DIC_STATUS_OK)
        return 0;

    params.width = (uint32_t)image.width;
    params.height = (uint32_t)image.height;
    params.components = (uint16_t)image.channels;
    params.decomposition_levels = FINALPROJ_LEVELS;
    params.reversible = 1u;
    params.multiple_component_transform = image.channels == 3 ? 1u : 0u;

    status = jp2_write_minimal_file(outputFileName, &params);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

int imageWriteJP2(
    const char *orgImageFileName,
    const char *outputFileName,
    int quality
)
{
    dic_image_u8 image = {0};
    dic_status status;

    if (orgImageFileName == NULL || outputFileName == NULL)
        return 0;

    status = dic_ppm_read(orgImageFileName, &image);
    if (status != DIC_STATUS_OK)
        return 0;

    status = j2k_write_image_jp2(outputFileName, &image, FINALPROJ_LEVELS, quality);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

int imageWriteJP2Tiled(
    const char *orgImageFileName,
    const char *outputFileName,
    int tileSize,
    int layers
)
{
    dic_image_u8 image = {0};
    dic_status status;
    int tile_width;
    int tile_height;

    if (orgImageFileName == NULL || outputFileName == NULL || tileSize < 0 || layers <= 0 || layers > 65535)
        return 0;

    status = dic_ppm_read(orgImageFileName, &image);
    if (status != DIC_STATUS_OK)
        return 0;

    tile_width = tileSize == 0 ? image.width : tileSize;
    tile_height = tileSize == 0 ? image.height : tileSize;

    status = j2k_write_image_jp2_tiled(
        outputFileName,
        &image,
        FINALPROJ_LEVELS,
        tile_width,
        tile_height,
        (uint16_t)layers
    );
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

int imageReadJ2K(
    const char *inputFileName,
    const char *outputFileName
)
{
    return imageReadJ2KLayers(inputFileName, outputFileName, 0);
}

int imageReadJ2KLayers(
    const char *inputFileName,
    const char *outputFileName,
    int maxLayers
)
{
    dic_image_u8 image = {0};
    dic_status status;

    if (inputFileName == NULL || outputFileName == NULL || maxLayers < 0 || maxLayers > 65535)
        return 0;
    status = j2k_read_image_codestream_layers(inputFileName, (uint16_t)maxLayers, &image);
    if (status == DIC_STATUS_OK)
        status = dic_ppm_write(outputFileName, &image);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

int imageReadJP2(
    const char *inputFileName,
    const char *outputFileName
)
{
    return imageReadJP2Layers(inputFileName, outputFileName, 0);
}

int imageReadJP2Layers(
    const char *inputFileName,
    const char *outputFileName,
    int maxLayers
)
{
    dic_image_u8 image = {0};
    dic_status status;

    if (inputFileName == NULL || outputFileName == NULL || maxLayers < 0 || maxLayers > 65535)
        return 0;
    status = j2k_read_image_jp2_layers(inputFileName, (uint16_t)maxLayers, &image);
    if (status == DIC_STATUS_OK)
        status = dic_ppm_write(outputFileName, &image);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

static int finalproj_print_j2k_info(const j2k_codestream_info *info)
{
    if (info == NULL)
        return 0;
    printf("width %u\n", (unsigned int)info->params.width);
    printf("height %u\n", (unsigned int)info->params.height);
    printf("components %u\n", (unsigned int)info->params.components);
    printf("levels %u\n", (unsigned int)info->params.decomposition_levels);
    printf("layers %u\n", (unsigned int)info->params.layers);
    printf("reversible %u\n", (unsigned int)info->params.reversible);
    printf("mct %u\n", (unsigned int)info->params.multiple_component_transform);
    printf("tile_width %u\n", (unsigned int)info->params.tile_width);
    printf("tile_height %u\n", (unsigned int)info->params.tile_height);
    printf("roi_shift %u\n", (unsigned int)info->params.roi_shift);
    printf("tile_payload_bytes %zu\n", info->tile_part_payload_bytes);
    printf("tile_parts %u\n", (unsigned int)info->tile_part_count);
    printf("total_tile_payload_bytes %zu\n", info->total_tile_part_payload_bytes);
    return 1;
}

int imageReadJ2KInfo(const char *inputFileName)
{
    j2k_codestream_info info;

    if (j2k_read_codestream_info(inputFileName, &info) != DIC_STATUS_OK)
        return 0;
    return finalproj_print_j2k_info(&info);
}

int imageReadJP2Info(const char *inputFileName)
{
    j2k_codestream_info info;

    if (jp2_read_codestream_info(inputFileName, &info) != DIC_STATUS_OK)
        return 0;
    return finalproj_print_j2k_info(&info);
}

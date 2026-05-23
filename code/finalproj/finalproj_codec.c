#include "finalproj/finalproj_codec.h"

#include <stdint.h>
#include <stdio.h>

#include "codec/dic_basic_codec.h"
#include "codec/dic_basic_file.h"
#include "codec/dic_metrics.h"
#include "codec/dic_roi_codec.h"
#include "codec/dic_snr_codec.h"
#include "codec/dic_tiled_codec.h"
#include "image_u8/image_u8.h"
#include "ppm/ppm.h"
#include "j2k/dic_j2k_codestream.h"
#include "j2k/dic_j2k_image.h"
#include "j2k/dic_j2k_parse.h"
#include "j2k/dic_jp2_file.h"

static long finalproj_file_size_bytes(const char *path)
{
    FILE *file = NULL;
    long size = -1;

#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0)
#else
    file = fopen(path, "rb");
#endif
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

double imageEncoderTiled(
    const char *orgImageFileName,
    int quantizationStepSize,
    int tileSize
)
{
    dic_image_u8 original = {0};
    dic_tiled_encoded_image encoded = {0};
    long bitstream_size;
    double bitrate = -1.0;

    if (orgImageFileName == NULL || quantizationStepSize <= 0 || tileSize <= 0)
        return -1.0;

    if (dic_ppm_read(orgImageFileName, &original) != DIC_STATUS_OK)
        return -1.0;

    if (dic_tiled_encode_image(
            original.data,
            original.width,
            original.height,
            original.channels,
            FINALPROJ_LEVELS,
            quantizationStepSize,
            tileSize,
            tileSize,
            &encoded) != DIC_STATUS_OK)
    {
        dic_image_u8_free(&original);
        return -1.0;
    }

    if (dic_tiled_write_file(FINALPROJ_TILED_BITSTREAM_PATH, &encoded) == DIC_STATUS_OK)
    {
        bitstream_size = finalproj_file_size_bytes(FINALPROJ_TILED_BITSTREAM_PATH);
        if (bitstream_size >= 0)
            bitrate = dic_metric_bitrate((size_t)bitstream_size * 8u, original.width, original.height);
    }

    dic_tiled_encoded_free(&encoded);
    dic_image_u8_free(&original);
    return bitrate;
}

double imageDecoderTiled(
    const char *bitstreamFileName,
    int quantizationStepSize,
    const char *orgImageFileName
)
{
    dic_tiled_encoded_image encoded = {0};
    dic_image_u8 original = {0};
    dic_image_u8 decoded = {0};
    const char *reconstruction_path;
    double psnr = -1.0;
    size_t sample_count;

    if (bitstreamFileName == NULL || orgImageFileName == NULL || quantizationStepSize <= 0)
        return -1.0;

    if (dic_tiled_read_file(bitstreamFileName, &encoded) != DIC_STATUS_OK)
        return -1.0;
    if (encoded.quant_step != quantizationStepSize)
    {
        dic_tiled_encoded_free(&encoded);
        return -1.0;
    }

    if (dic_tiled_decode_image(&encoded, &decoded) != DIC_STATUS_OK)
    {
        dic_tiled_encoded_free(&encoded);
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
    dic_tiled_encoded_free(&encoded);
    return psnr;
}

double imageEncoderSNR(
    const char *orgImageFileName,
    int quantizationStepSize
)
{
    dic_image_u8 original = {0};
    dic_snr_encoded_image encoded = {0};
    long bitstream_size;
    double bitrate = -1.0;

    if (orgImageFileName == NULL || quantizationStepSize <= 0)
        return -1.0;

    if (dic_ppm_read(orgImageFileName, &original) != DIC_STATUS_OK)
        return -1.0;

    if (dic_snr_encode_image(
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

    if (dic_snr_write_file(FINALPROJ_SNR_BITSTREAM_PATH, &encoded) == DIC_STATUS_OK)
    {
        bitstream_size = finalproj_file_size_bytes(FINALPROJ_SNR_BITSTREAM_PATH);
        if (bitstream_size >= 0)
            bitrate = dic_metric_bitrate((size_t)bitstream_size * 8u, original.width, original.height);
    }

    dic_snr_encoded_free(&encoded);
    dic_image_u8_free(&original);
    return bitrate;
}

double imageDecoderSNR(
    const char *bitstreamFileName,
    int quantizationStepSize,
    int decodedBitplanes,
    const char *orgImageFileName
)
{
    dic_snr_encoded_image encoded = {0};
    dic_image_u8 original = {0};
    dic_image_u8 decoded = {0};
    const char *reconstruction_path;
    double psnr = -1.0;
    size_t sample_count;

    if (bitstreamFileName == NULL
        || orgImageFileName == NULL
        || quantizationStepSize <= 0
        || decodedBitplanes < 0)
    {
        return -1.0;
    }

    if (dic_snr_read_file(bitstreamFileName, &encoded) != DIC_STATUS_OK)
        return -1.0;
    if (encoded.quant_step != quantizationStepSize)
    {
        dic_snr_encoded_free(&encoded);
        return -1.0;
    }

    if (dic_snr_decode_image(&encoded, decodedBitplanes, &decoded) != DIC_STATUS_OK)
    {
        dic_snr_encoded_free(&encoded);
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
    dic_snr_encoded_free(&encoded);
    return psnr;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex H.2-H.3, Maxshift ROI coding and ROI mask generation. */
double imageEncoderROI(
    const char *orgImageFileName,
    int quantizationStepSize
)
{
    dic_image_u8 original = {0};
    dic_roi_encoded_image encoded = {0};
    long bitstream_size;
    double bitrate = -1.0;

    if (orgImageFileName == NULL || quantizationStepSize <= 0)
        return -1.0;

    if (dic_ppm_read(orgImageFileName, &original) != DIC_STATUS_OK)
        return -1.0;

    if (dic_roi_encode_image(
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

    if (dic_roi_write_file(FINALPROJ_ROI_BITSTREAM_PATH, &encoded) == DIC_STATUS_OK)
    {
        bitstream_size = finalproj_file_size_bytes(FINALPROJ_ROI_BITSTREAM_PATH);
        if (bitstream_size >= 0)
            bitrate = dic_metric_bitrate((size_t)bitstream_size * 8u, original.width, original.height);
    }

    dic_roi_encoded_free(&encoded);
    dic_image_u8_free(&original);
    return bitrate;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex H.1-H.2, ROI decoding and Maxshift coefficient realignment. */
double imageDecoderROI(
    const char *bitstreamFileName,
    int quantizationStepSize,
    int decodedBitplanes,
    const char *orgImageFileName
)
{
    dic_roi_encoded_image encoded = {0};
    dic_image_u8 original = {0};
    dic_image_u8 decoded = {0};
    const char *reconstruction_path;
    double psnr = -1.0;
    size_t sample_count;

    if (bitstreamFileName == NULL
        || orgImageFileName == NULL
        || quantizationStepSize <= 0
        || decodedBitplanes < 0)
    {
        return -1.0;
    }

    if (dic_roi_read_file(bitstreamFileName, &encoded) != DIC_STATUS_OK)
        return -1.0;
    if (encoded.quant_step != quantizationStepSize)
    {
        dic_roi_encoded_free(&encoded);
        return -1.0;
    }

    if (dic_roi_decode_image(&encoded, decodedBitplanes, &decoded) != DIC_STATUS_OK)
    {
        dic_roi_encoded_free(&encoded);
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
    dic_roi_encoded_free(&encoded);
    return psnr;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.4 and B.10.3, codestream structure with empty packet payload. */
int imageWriteJ2KStub(
    const char *orgImageFileName,
    const char *outputFileName
)
{
    dic_image_u8 image = {0};
    dic_j2k_basic_params params = {0};
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

    status = dic_j2k_write_empty_packet_codestream(outputFileName, &params);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G, Annex F, Annex D, and Annex B.10; PPM samples are level shifted, transformed, EBCOT coded, packetized, and written as SOD payload. */
int imageWriteJ2K(
    const char *orgImageFileName,
    const char *outputFileName
)
{
    dic_image_u8 image = {0};
    dic_status status;

    if (orgImageFileName == NULL || outputFileName == NULL)
        return 0;

    status = dic_ppm_read(orgImageFileName, &image);
    if (status != DIC_STATUS_OK)
        return 0;

    status = dic_j2k_write_image_codestream(outputFileName, &image, FINALPROJ_LEVELS);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5, JP2 required boxes and contiguous codestream box. */
int imageWriteJP2Stub(
    const char *orgImageFileName,
    const char *outputFileName
)
{
    dic_image_u8 image = {0};
    dic_j2k_basic_params params = {0};
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

    status = dic_jp2_write_minimal_file(outputFileName, &params);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1 and Annex I.5.3, JP2 wraps the same payload-bearing codestream in required JP2 boxes. */
int imageWriteJP2(
    const char *orgImageFileName,
    const char *outputFileName
)
{
    dic_image_u8 image = {0};
    dic_status status;

    if (orgImageFileName == NULL || outputFileName == NULL)
        return 0;

    status = dic_ppm_read(orgImageFileName, &image);
    if (status != DIC_STATUS_OK)
        return 0;

    status = dic_j2k_write_image_jp2(outputFileName, &image, FINALPROJ_LEVELS);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.5 and B.10.8, JP2 may wrap a codestream with one full-image tile or multiple independently coded tiles and quality layers. */
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

    status = dic_j2k_write_image_jp2_tiled(
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

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A, Annex B.10, Annex D, Annex F, and Annex G; raw J2K codestreams decode back to project PGM/PPM images. */
int imageReadJ2K(
    const char *inputFileName,
    const char *outputFileName
)
{
    return imageReadJ2KLayers(inputFileName, outputFileName, 0);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.12, quality-layer progression allows reconstructing a codestream prefix that ends after an LRCP layer. */
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
    status = dic_j2k_read_image_codestream_layers(inputFileName, (uint16_t)maxLayers, &image);
    if (status == DIC_STATUS_OK)
        status = dic_ppm_write(outputFileName, &image);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1; JP2 decoding locates the contiguous codestream box before applying the J2K image decoder. */
int imageReadJP2(
    const char *inputFileName,
    const char *outputFileName
)
{
    return imageReadJP2Layers(inputFileName, outputFileName, 0);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.12 and Annex I.5.3, JP2 layer preview decodes the jp2c codestream prefix through the requested quality layer. */
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
    status = dic_j2k_read_image_jp2_layers(inputFileName, (uint16_t)maxLayers, &image);
    if (status == DIC_STATUS_OK)
        status = dic_ppm_write(outputFileName, &image);
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A marker segments expose codestream dimensions and coding style. */
static int finalproj_print_j2k_info(const dic_j2k_codestream_info *info)
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

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.6, raw J2K codestream metadata parser. */
int imageReadJ2KInfo(const char *inputFileName)
{
    dic_j2k_codestream_info info;

    if (dic_j2k_read_codestream_info(inputFileName, &info) != DIC_STATUS_OK)
        return 0;
    return finalproj_print_j2k_info(&info);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1, JP2 Contiguous Codestream box metadata parser. */
int imageReadJP2Info(const char *inputFileName)
{
    dic_j2k_codestream_info info;

    if (dic_jp2_read_codestream_info(inputFileName, &info) != DIC_STATUS_OK)
        return 0;
    return finalproj_print_j2k_info(&info);
}

/**
 * @file finalproj_codec.c
 * @brief Assignment-facing codec API plus JPEG 2000 demo helpers.
 *
 * Impl imageEncoder writes image.bit with the basic 5/3-DWT, quantization,
 * prediction, scan/EZT, and Huffman path; imageDecoder reconstructs
 * image_recon.pgm or image_recon.ppm and reports PSNR.
 */

#include "finalproj/finalproj_codec.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "codec/metrics.h"
#include "fs/fs.h"
#include "image_u8/image_u8.h"
#include "j2k/j2k_codestream.h"
#include "j2k/j2k_image.h"
#include "j2k/j2k_parse.h"
#include "j2k/jp2_file.h"
#include "ppm/ppm.h"

static long finalproj_file_size_bytes(const char* path) {
    FILE* file = NULL;
    long size = -1;

    file = fs_open_file(path, "rb");
    if (file == NULL) return -1;

    if (fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    fclose(file);
    return size;
}

double imageEncoder(const char* orgImageFileName, float quantizationStepSize,
                    const char* outputFileName) {
    dic_image_u8 original = {0};
    codec_basic_encoded_image encoded = {0};
    dic_status status;
    long bitstream_size;
    double bitrate = -1.0;

    if (orgImageFileName == NULL || outputFileName == NULL ||
        !isfinite(quantizationStepSize) || quantizationStepSize <= 0.0f) {
        fprintf(stderr, "error: invalid arguments to imageEncoder\n");
        return -1.0;
    }

    status = dic_ppm_read(orgImageFileName, &original);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to read input image '%s': %s\n",
                orgImageFileName, dic_status_message(status));
        return -1.0;
    }

    status = codec_basic_encode_image(
        original.data, original.width, original.height, original.channels,
        FINALPROJ_LEVELS, quantizationStepSize, 0, &encoded);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: encode failed: %s\n",
                dic_status_message(status));
        dic_image_u8_free(&original);
        return -1.0;
    }

    status = codec_basic_write_file(outputFileName, &encoded);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to write bitstream '%s': %s\n",
                outputFileName, dic_status_message(status));
    } else {
        bitstream_size = finalproj_file_size_bytes(outputFileName);
        if (bitstream_size >= 0)
            bitrate = codec_metric_bitrate((size_t)bitstream_size * 8u,
                                           original.width, original.height);
    }

    codec_basic_encoded_free(&encoded);
    dic_image_u8_free(&original);
    return bitrate;
}

double imageDecoder(const char* bitstreamFileName, float quantizationStepSize,
                    const char* orgImageFileName) {
    codec_basic_encoded_image encoded = {0};
    dic_image_u8 original = {0};
    dic_image_u8 decoded = {0};
    dic_status status;
    const char* reconstruction_path;
    double psnr = -1.0;
    size_t sample_count;

    if (bitstreamFileName == NULL || orgImageFileName == NULL ||
        !isfinite(quantizationStepSize) || quantizationStepSize <= 0.0f) {
        fprintf(stderr, "error: invalid arguments to imageDecoder\n");
        return -1.0;
    }

    status = codec_basic_read_file(bitstreamFileName, &encoded);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to read bitstream '%s': %s\n",
                bitstreamFileName, dic_status_message(status));
        return -1.0;
    }

    if (encoded.quant_step != quantizationStepSize) {
        fprintf(stderr,
                "error: quant_step mismatch: bitstream was encoded with "
                "quant=%.9g but --quant %.9g was specified\n",
                encoded.quant_step, quantizationStepSize);
        codec_basic_encoded_free(&encoded);
        return -1.0;
    }

    status = codec_basic_decode_image(&encoded, encoded.levels, 0, &decoded);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: decode failed: %s\n",
                dic_status_message(status));
        codec_basic_encoded_free(&encoded);
        return -1.0;
    }

    printf("Loading original image %s\n", orgImageFileName);
    status = dic_ppm_read(orgImageFileName, &original);
    if (status == DIC_STATUS_OK && original.width == decoded.width &&
        original.height == decoded.height &&
        original.channels == decoded.channels) {
        sample_count = dic_image_u8_sample_count(
            original.width, original.height, original.channels);
        psnr = codec_metric_psnr_u8(original.data, decoded.data, sample_count);
    } else if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to read original image '%s': %s\n",
                orgImageFileName, dic_status_message(status));
    } else {
        fprintf(stderr,
                "error: original image dimensions (%dx%d, %d channels) do not "
                "match decoded image (%dx%d, %d channels)\n",
                original.width, original.height, original.channels,
                decoded.width, decoded.height, decoded.channels);
    }

    reconstruction_path = decoded.channels == 1 ? FINALPROJ_RECON_GRAY_PATH
                                                : FINALPROJ_RECON_RGB_PATH;
    status = dic_ppm_write(reconstruction_path, &decoded);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to write reconstruction '%s': %s\n",
                reconstruction_path, dic_status_message(status));
        psnr = -1.0;
    }
    dic_image_u8_free(&original);
    dic_image_u8_free(&decoded);
    codec_basic_encoded_free(&encoded);
    return psnr;
}

int imageWriteJ2K(const char* orgImageFileName, const char* outputFileName,
                  int quality) {
    dic_image_u8 image = {0};
    dic_status status;

    if (orgImageFileName == NULL || outputFileName == NULL) return 0;

    status = dic_ppm_read(orgImageFileName, &image);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to read input image '%s': %s\n",
                orgImageFileName, dic_status_message(status));
        return 0;
    }

    status = j2k_write_image_codestream(outputFileName, &image,
                                        FINALPROJ_LEVELS, quality);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to write J2K codestream '%s': %s\n",
                outputFileName, dic_status_message(status));
    }
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

int imageWriteJP2(const char* orgImageFileName, const char* outputFileName,
                  int quality) {
    dic_image_u8 image = {0};
    dic_status status;

    if (orgImageFileName == NULL || outputFileName == NULL) return 0;

    status = dic_ppm_read(orgImageFileName, &image);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to read input image '%s': %s\n",
                orgImageFileName, dic_status_message(status));
        return 0;
    }

    status =
        j2k_write_image_jp2(outputFileName, &image, FINALPROJ_LEVELS, quality);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to write JP2 file '%s': %s\n",
                outputFileName, dic_status_message(status));
    }
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

int imageWriteJP2Tiled(const char* orgImageFileName, const char* outputFileName,
                       int tileSize, int layers) {
    dic_image_u8 image = {0};
    dic_status status;
    int tile_width;
    int tile_height;

    if (orgImageFileName == NULL || outputFileName == NULL || tileSize < 0 ||
        layers <= 0 || layers > 65535)
        return 0;

    status = dic_ppm_read(orgImageFileName, &image);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to read input image '%s': %s\n",
                orgImageFileName, dic_status_message(status));
        return 0;
    }

    tile_width = tileSize == 0 ? image.width : tileSize;
    tile_height = tileSize == 0 ? image.height : tileSize;

    status =
        j2k_write_image_jp2_tiled(outputFileName, &image, FINALPROJ_LEVELS,
                                  tile_width, tile_height, (uint16_t)layers);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to write tiled JP2 '%s': %s\n",
                outputFileName, dic_status_message(status));
    }
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

int imageReadJ2K(const char* inputFileName, const char* outputFileName) {
    return imageReadJ2KLayers(inputFileName, outputFileName, 0);
}

int imageReadJ2KLayers(const char* inputFileName, const char* outputFileName,
                       int maxLayers) {
    dic_image_u8 image = {0};
    dic_status status;

    if (inputFileName == NULL || outputFileName == NULL || maxLayers < 0 ||
        maxLayers > 65535)
        return 0;

    status = j2k_read_image_codestream_layers(inputFileName,
                                              (uint16_t)maxLayers, &image);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to read J2K codestream '%s': %s\n",
                inputFileName, dic_status_message(status));
        return 0;
    }

    status = dic_ppm_write(outputFileName, &image);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to write output image '%s': %s\n",
                outputFileName, dic_status_message(status));
    }
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

int imageReadJP2(const char* inputFileName, const char* outputFileName) {
    return imageReadJP2Layers(inputFileName, outputFileName, 0);
}

int imageReadJP2Layers(const char* inputFileName, const char* outputFileName,
                       int maxLayers) {
    dic_image_u8 image = {0};
    dic_status status;

    if (inputFileName == NULL || outputFileName == NULL || maxLayers < 0 ||
        maxLayers > 65535)
        return 0;

    status =
        j2k_read_image_jp2_layers(inputFileName, (uint16_t)maxLayers, &image);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to read JP2 file '%s': %s\n",
                inputFileName, dic_status_message(status));
        return 0;
    }

    status = dic_ppm_write(outputFileName, &image);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to write output image '%s': %s\n",
                outputFileName, dic_status_message(status));
    }
    dic_image_u8_free(&image);
    return status == DIC_STATUS_OK;
}

static int finalproj_print_j2k_info(const j2k_codestream_info* info) {
    if (info == NULL) return 0;
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
    printf("total_tile_payload_bytes %zu\n",
           info->total_tile_part_payload_bytes);
    return 1;
}

int imageReadJ2KInfo(const char* inputFileName) {
    j2k_codestream_info info;

    if (j2k_read_codestream_info(inputFileName, &info) != DIC_STATUS_OK)
        return 0;
    return finalproj_print_j2k_info(&info);
}

int imageReadJP2Info(const char* inputFileName) {
    j2k_codestream_info info;

    if (jp2_read_codestream_info(inputFileName, &info) != DIC_STATUS_OK)
        return 0;
    return finalproj_print_j2k_info(&info);
}

int imageReadBitInfo(const char* bitstreamFileName) {
    codec_basic_encoded_image encoded = {0};
    dic_status status;
    long file_size;
    int ch, res;

    if (bitstreamFileName == NULL) {
        fprintf(stderr, "error: invalid arguments to imageReadBitInfo\n");
        return 0;
    }

    file_size = finalproj_file_size_bytes(bitstreamFileName);
    if (file_size < 0) {
        fprintf(stderr, "error: cannot access '%s'\n", bitstreamFileName);
        return 0;
    }

    status = codec_basic_read_file(bitstreamFileName, &encoded);
    if (status != DIC_STATUS_OK) {
        fprintf(stderr, "error: failed to read bitstream '%s': %s\n",
                bitstreamFileName, dic_status_message(status));
        return 0;
    }

    printf("file         %s\n", bitstreamFileName);
    printf("file_bytes   %ld\n", file_size);
    printf("version      %u\n", DIC_BASIC_FILE_VERSION);
    printf("width        %d\n", encoded.width);
    printf("height       %d\n", encoded.height);
    printf("channels     %d\n", encoded.channels);
    printf("levels       %d\n", encoded.levels);
    printf("quant_step   %.9g\n", encoded.quant_step);

    for (ch = 0; ch < encoded.channels; ++ch) {
        const codec_basic_channel_stream* stream = encoded.channel_streams + ch;
        printf("channel_%d_resolutions %d\n", ch, stream->num_resolutions);

        for (res = 0; res < stream->num_resolutions; ++res) {
            const codec_basic_resolution_stream* rs = stream->resolutions + res;
            printf("channel_%d_res_%d_bitplanes %d\n", ch, res,
                   rs->num_bitplanes);
            printf("channel_%d_res_%d_bytes    %zu\n", ch, res,
                   codec_basic_resolution_byte_size(rs));
        }
    }

    codec_basic_encoded_free(&encoded);
    return 1;
}

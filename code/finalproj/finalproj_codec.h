#pragma once
/**
 * @file finalproj_codec.h
 * @brief Assignment-facing wrappers around the basic codec and JPEG 2000.
 *
 * @code{.unparsed}
 * imageEncoder:
 *   PGM/PPM -> codec_basic_encode_image -> DICW v10 file
 *
 * imageDecoder:
 *   DICW v10 file -> codec_basic_decode_image -> image_recon.pgm/.ppm
 * @endcode
 */

#include <stddef.h>

#include "codec/scan.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default DICW output used by the `bit codec` command. */
#define FINALPROJ_BITSTREAM_PATH "image.bit"
/** Default grayscale reconstruction path. */
#define FINALPROJ_RECON_GRAY_PATH "image_recon.pgm"
/** Default RGB reconstruction path. */
#define FINALPROJ_RECON_RGB_PATH "image_recon.ppm"
/** Wavelet decomposition level count used by assignment wrappers. */
#define FINALPROJ_LEVELS 5

/** Metrics and Huffman statistics produced by one encode/decode round trip. */
typedef struct finalproj_codec_report {
    double bitrate;
    double compression_ratio;
    double psnr;
    size_t huffman_symbol_counts[DIC_SCAN_TOKEN_COUNT];
    double huffman_probabilities[DIC_SCAN_TOKEN_COUNT];
} finalproj_codec_report;

/**
 * @brief Encodes an image and writes a DICW v10 progressive bitstream.
 * @param orgImageFileName Input PGM or PPM path.
 * @param quantizationStepSize Finite positive scalar quantization step.
 * @param outputFileName Destination DICW path.
 * @return Encoded bits per pixel, or -1.0 on failure.
 */
double imageEncoder(const char* orgImageFileName, float quantizationStepSize,
                    const char* outputFileName);

/**
 * @brief Runs the basic codec and returns machine-readable evaluation metrics.
 */
int imageCodecReport(const char* orgImageFileName, float quantizationStepSize,
                     const char* outputFileName,
                     finalproj_codec_report* report);

/**
 * @brief Parses DICW metadata and prints per-channel payload byte totals.
 * @param bitstreamFileName DICW v10 source path.
 * @return Nonzero on success, zero on failure.
 */
int imageReadBitInfo(const char* bitstreamFileName);

/**
 * @brief Decodes all DICW layers and writes the reconstruction.
 * @param bitstreamFileName DICW v10 source path.
 * @param orgImageFileName Reference PGM/PPM used to calculate PSNR.
 * @return PSNR in dB, infinity for an exact reconstruction, or -1.0 on
 * failure.
 */
double imageDecoder(const char* bitstreamFileName,
                    const char* orgImageFileName);

/**
 * @brief Writes a raw J2K codestream.
 * @param orgImageFileName Input PGM/PPM path.
 * @param outputFileName Destination `.j2k` path.
 * @param quality -1 for reversible coding, or 1..100 for irreversible 9/7.
 * @return Nonzero on success, zero on failure.
 */
int imageWriteJ2K(const char* orgImageFileName, const char* outputFileName,
                  int quality);

/** @brief Writes a JP2 file; parameters follow imageWriteJ2K(). */
int imageWriteJP2(const char* orgImageFileName, const char* outputFileName,
                  int quality);

/**
 * @brief Writes tiled JP2 with a requested quality-layer count.
 * @param orgImageFileName Input PGM/PPM path.
 * @param outputFileName Destination JP2 path.
 * @param tileSize Square tile side, or zero for one full-image tile.
 * @param layers Number of JPEG 2000 quality layers in 1..65535.
 * @return Nonzero on success, zero on failure.
 */
int imageWriteJP2Tiled(const char* orgImageFileName, const char* outputFileName,
                       int tileSize, int layers);

/** @brief Decodes all layers of a raw J2K codestream to PGM or PPM. */
int imageReadJ2K(const char* inputFileName, const char* outputFileName);

/**
 * @brief Decodes a quality prefix of a raw J2K codestream.
 * @param inputFileName Input raw codestream path.
 * @param outputFileName Destination PGM/PPM path.
 * @param maxLayers Maximum layers to decode, or zero for all layers.
 * @return Nonzero on success, zero on failure.
 */
int imageReadJ2KLayers(const char* inputFileName, const char* outputFileName,
                       int maxLayers);

/** @brief Decodes all layers of a JP2 file to PGM or PPM. */
int imageReadJP2(const char* inputFileName, const char* outputFileName);

/**
 * @brief Decodes a quality prefix of a JP2 file.
 * @param inputFileName Input JP2 path.
 * @param outputFileName Destination PGM/PPM path.
 * @param maxLayers Maximum layers to decode, or zero for all layers.
 * @return Nonzero on success, zero on failure.
 */
int imageReadJP2Layers(const char* inputFileName, const char* outputFileName,
                       int maxLayers);

/** Prints raw J2K codestream metadata to stdout. */
int imageReadJ2KInfo(const char* inputFileName);

/** Prints JP2 codestream metadata to stdout. */
int imageReadJP2Info(const char* inputFileName);

#ifdef __cplusplus
}
#endif

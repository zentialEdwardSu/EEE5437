#pragma once
/**
 * @file finalproj_net.h
 * @brief File-backed, quality-progressive DICW transport over TCP.
 *
 * The sender fully encodes and stages a DICW stream before opening the TCP
 * connection. The receiver appends incoming chunks to a temporary file and
 * decodes a quality layer as soon as its layer marker is available.
 *
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encodes, stages, and sends an image as a layer-major DICW stream.
 *
 * @code{.unparsed}
 * input PGM/PPM -> encode -> temporary DICW file
 *                              |
 *                              `-> buffered file reads -> TCP
 * @endcode
 *
 * @param inputFile PGM or PPM source path.
 * @param host Destination host.
 * @param port Destination TCP port.
 * @param quant Finite positive quantization step.
 * @param rateLimit Maximum bytes per second, or zero for unlimited.
 * @return Nonzero on complete transfer, zero on failure.
 */
int networkSend(const char* inputFile, const char* host, int port, float quant,
                uint32_t rateLimit);

/**
 * @brief Receives DICW chunks and publishes each complete quality layer.
 *
 * Every chunk is appended to a temporary file before parsing. The parser
 * resumes at its previous byte offset and consumes only complete bit-plane
 * blocks. After a `0xfffffffe` layer marker, the available prefix is decoded
 * and atomically written to @p outputFile.
 *
 * @param port Local TCP listen port.
 * @param outputFile Reconstruction path, replaced after every complete layer.
 * @param originalFile Optional reference image for per-layer PSNR, or NULL.
 * @return Nonzero after the exact payload is parsed, zero on failure.
 */
int networkReceive(int port, const char* outputFile,
                   const char* originalFile);

#ifdef __cplusplus
}
#endif

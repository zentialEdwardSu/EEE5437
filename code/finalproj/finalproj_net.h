#pragma once

/**
 * @file finalproj_net.h
 * @brief Network send/receive bridge between CLI, lib_net, and codec.
 *
 * Provides networkSend() and networkReceive() that encode/serialize an image
 * and transmit it over TCP, with optional rate limiting and progressive
 * decode display on the receiving side.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * Encodes an image, serializes it, and sends over TCP.
 *
 * @param inputFile  Path to input PGM or PPM image.
 * @param host       Destination hostname or IP address.
 * @param port       Destination port.
 * @param quant      Positive quantization step.
 * @param rateLimit  Send rate limit in bytes/sec, 0 = unlimited.
 * @return 1 on success, 0 on failure.
 */
int networkSend(const char *inputFile, const char *host, int port,
                int quant, uint32_t rateLimit);

/**
 * Listens for an encoded image over TCP, decodes progressively, saves output.
 *
 * Prints progressive PSNR information to stdout during decode.
 *
 * @param port          Listen port.
 * @param outputFile    Path for reconstructed output PGM or PPM.
 * @param quant         Positive quantization step (must match sender).
 * @param originalFile  Optional original image for PSNR comparison (NULL = none).
 * @return 1 on success, 0 on failure.
 */
int networkReceive(int port, const char *outputFile, int quant,
                   const char *originalFile);

#ifdef __cplusplus
}
#endif

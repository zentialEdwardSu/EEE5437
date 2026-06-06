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

typedef enum finalproj_scaling_mode {
    FINALPROJ_SCALING_SNR = 1,
    FINALPROJ_SCALING_RESOLUTION = 2
} finalproj_scaling_mode;

/**
 * Encodes an image, serializes it, and sends over TCP.
 *
 * @param inputFile  Path to input PGM or PPM image.
 * @param host       Destination hostname or IP address.
 * @param port       Destination port.
 * @param quant      Positive quantization step.
 * @param rateLimit  Send rate limit in bytes/sec, 0 = unlimited.
 * @param scalingMode SNR quality layers or resolution layers.
 * @return 1 on success, 0 on failure.
 */
int networkSend(const char* inputFile, const char* host, int port, float quant,
                uint32_t rateLimit, finalproj_scaling_mode scalingMode);

/**
 * Listens for an encoded image over TCP, decodes progressively, saves output.
 *
 * Prints progressive PSNR information to stdout during decode.
 *
 * @param port          Listen port.
 * @param outputFile    Path for reconstructed output PGM or PPM.
 * @param quant         Positive quantization step (must match sender).
 * @param originalFile  Optional original image for PSNR comparison (NULL =
 * none).
 * @param scalingMode   Expected SNR or resolution progression mode.
 * @return 1 on success, 0 on failure.
 */
int networkReceive(int port, const char* outputFile, float quant,
                   const char* originalFile,
                   finalproj_scaling_mode scalingMode);

#ifdef __cplusplus
}
#endif

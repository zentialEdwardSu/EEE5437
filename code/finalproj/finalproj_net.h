#pragma once
/**
 * @file finalproj_net.h
 * @brief File-backed, quality-progressive DICW transport over TCP.
 *
 * The sender fully encodes and stages a DICW stream before opening the TCP
 * connection. The receiver appends incoming chunks to a temporary file and
 * decodes a quality layer as soon as its layer marker is available.
 *
 * @code{.unparsed}
 * TCP byte stream
 * +--------------------------+--------------------------------------+
 * | payload_size             | DICW payload                         |
 * | u32 little-endian        | payload_size bytes                   |
 * +--------------------------+--------------------------------------+
 *
 * DICW payload
 * +-------------------------------+-------------------------------+
 * | magic "DICW"                  | 4 bytes                       |
 * | version                       | u32 LE                        |
 * | width, height                 | 2 x u32 LE                    |
 * | channels, DWT levels          | 2 x u32 LE                    |
 * | quant_step IEEE-754 bits      | u32 LE                        |
 * | Huffman code lengths          | DIC_SCAN_TOKEN_COUNT bytes    |
 * | maximum quality layers        | u32 LE                        |
 * | bitplane count per channel    | channels x u32 LE             |
 * +-------------------------------+-------------------------------+
 * | layer 0: channel bit-planes   | channels owning layer 0       |
 * | layer marker 0xfffffffe       | u32 LE                        |
 * | layer 1: channel bit-planes   | channels owning layer 1       |
 * | layer marker 0xfffffffe       | u32 LE                        |
 * | ...                           |                               |
 * +-------------------------------+-------------------------------+
 *
 * The payload is exactly the layer-major DICW format documented in
 * codec/basic_file.h, so files and network transfers share one protocol.
 * @endcode
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

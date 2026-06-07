#pragma once
/**
 * @file finalproj_net.h
 * @brief File-backed, quality-progressive DICQ transport over TCP.
 *
 * The sender fully encodes and stages a DICQ stream before opening the TCP
 * connection. The receiver appends incoming chunks to a temporary file and
 * decodes a quality layer as soon as its layer marker is available.
 *
 * @code{.unparsed}
 * TCP byte stream
 * +--------------------------+--------------------------------------+
 * | payload_size             | DICQ payload                         |
 * | u32 little-endian        | payload_size bytes                   |
 * +--------------------------+--------------------------------------+
 *
 * DICQ payload
 * +-------------------------------+-------------------------------+
 * | magic "DICQ"                  | 4 bytes                       |
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
 * Each bit-plane block uses the field layout documented in
 * codec/basic_file.h. DICQ changes DICW channel-major ordering to layer-major
 * ordering so progressive reconstruction can begin early.
 * @endcode
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encodes, stages, and sends an image as a layer-major DICQ stream.
 *
 * @code{.unparsed}
 * input PGM/PPM -> encode -> temporary DICQ file
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
 * @brief Receives DICQ chunks and publishes each complete quality layer.
 *
 * Every chunk is appended to a temporary file before parsing. The parser
 * resumes at its previous byte offset and consumes only complete bit-plane
 * blocks. After a `0xfffffffe` layer marker, the available prefix is decoded
 * and atomically written to @p outputFile.
 *
 * @param port Local TCP listen port.
 * @param outputFile Reconstruction path, replaced after every complete layer.
 * @param quant Expected quantization step; the stream value is authoritative.
 * @param originalFile Optional reference image for per-layer PSNR, or NULL.
 * @return Nonzero after the exact payload is parsed, zero on failure.
 */
int networkReceive(int port, const char* outputFile, float quant,
                   const char* originalFile);

#ifdef __cplusplus
}
#endif

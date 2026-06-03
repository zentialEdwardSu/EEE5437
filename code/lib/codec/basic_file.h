#pragma once
/**
 * @file basic_file.h
 * @brief File and stream serialization for the basic codec bitstream.
 */

#include <stdio.h>

#include "codec/basic_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIC_BASIC_FILE_MAGIC "DICW"
#define DIC_BASIC_FILE_VERSION 1u

/**
 * @brief Writes an encoded image to a DICW bitstream file.
 * @param path Output path.
 * @param encoded Encoded image to serialize.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_write_file(
    const char *path,
    const codec_basic_encoded_image *encoded
);

/**
 * @brief Reads an encoded image from a DICW bitstream file.
 * @param path Input path.
 * @param encoded Output encoded image; existing contents are freed before reading.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_read_file(
    const char *path,
    codec_basic_encoded_image *encoded
);

/**
 * @brief Writes an encoded image to an already-open binary stream.
 * @param file Writable binary stream.
 * @param encoded Encoded image to serialize.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_write_stream(
    FILE *file,
    const codec_basic_encoded_image *encoded
);

/**
 * @brief Reads an encoded image from an already-open binary stream.
 * @param file Readable binary stream positioned at the DICW header.
 * @param encoded Output encoded image; existing contents are freed before reading.
 * @return DIC_STATUS_OK on success, otherwise an error status.
 */
dic_status codec_basic_read_stream(
    FILE *file,
    codec_basic_encoded_image *encoded
);

#ifdef __cplusplus
}
#endif

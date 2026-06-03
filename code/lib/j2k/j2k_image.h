#pragma once

/**
 * @file j2k_image.h
 * @brief JPEG 2000 image encoding and decoding API.
 *
 * Top-level API for encoding images to J2K/JP2 codestreams and decoding
 * codestreams back to images. This module orchestrates the full JPEG 2000
 * codec pipeline:
 *
 * Encoding path (lossless, quality = -1):
 *   -# Level shift unsigned 8-bit samples to signed (Annex G)
 *   -# Reversible Component Transform for RGB → YDbDr (Annex G.1)
 *   -# 5-3 reversible wavelet transform (Annex F.3)
 *   -# Code-block partitioning and EBCOT coding (Annex B.7, Annex D)
 *   -# Packet construction with LRCP layer/resolution/component progression
 *      (Annex B.10.8)
 *   -# Codestream marker segments: SOC, SIZ, COD, QCD, SOT/SOD, EOC
 *      (Annex A.3-A.5)
 *
 * Encoding path (lossy, quality 1–100):
 *   -# Level shift and ICT (Annex G.2) for colour images
 *   -# 9-7 irreversible wavelet transform (Annex F.4)
 *   -# Scalar quantization per sub-band (Annex E.1)
 *   -# EBCOT coding and packet construction as above
 *   -# QCD marker with irreversible SPqcd fields (Annex A.6.4)
 *
 * ROI encoding (lossless path with roi_shift > 0):
 *   -# ROI Maxshift mask construction (Annex H)
 *   -# Coefficient-domain magnitude scaling before EBCOT
 *   -# RGN marker segment emission (Annex A.8.4)
 *
 * Decoding path:
 *   -# Codestream/JP2 parsing (Annex A, Annex I)
 *   -# Packet header decoding with tag-tree inclusion/zero-bit-plane
 *      recovery (Annex B.10)
 *   -# EBCOT code-block decoding (Annex D)
 *   -# Dequantization for irreversible streams (Annex E.1)
 *   -# Inverse DWT (5-3 or 9-7, Annex F)
 *   -# Inverse RCT/ICT (Annex G)
 *   -# Level shift back to unsigned 8-bit
 *
 * References:
 * - paper/T-REC-T.800-200208.pdf, Annex A (codestream syntax)
 * - paper/T-REC-T.800-200208.pdf, Annex B (image geometry and packets)
 * - paper/T-REC-T.800-200208.pdf, Annex D (EBCOT)
 * - paper/T-REC-T.800-200208.pdf, Annex E (quantization)
 * - paper/T-REC-T.800-200208.pdf, Annex F (DWT)
 * - paper/T-REC-T.800-200208.pdf, Annex G (component transforms)
 * - paper/T-REC-T.800-200208.pdf, Annex H (ROI)
 * - paper/T-REC-T.800-200208.pdf, Annex I (JP2)
 */

#include <stdint.h>

#include "codec/subband.h"
#include "errors/errors.h"
#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encode an image to a raw J2K codestream file.
 *
 * Pipeline: level shift → {RCT + 5-3 DWT} if quality == -1, or
 * {ICT + 9-7 DWT + scalar quantization} if quality ∈ [1, 100] →
 * EBCOT code-block coding → LRCP packet construction → codestream
 * marker emission.
 *
 * @param path Output file path for the .j2k codestream.
 * @param image Source 8-bit image (1 or 3 channels).
 * @param requested_levels Number of DWT decomposition levels (≥ 1).
 * @param quality -1 for lossless (5-3/RCT), 1–100 for lossy (9-7/ICT).
 * @return DIC_STATUS_OK on success.
 * @return DIC_STATUS_INVALID_ARGUMENT for NULL, zero-size, or invalid components.
 * @return DIC_STATUS_IO_ERROR if file I/O fails.
 * @return DIC_STATUS_MEMORY_ERROR if allocations fail.
 */
dic_status j2k_write_image_codestream(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int quality
);

/**
 * @brief Encode a tiled image to a raw J2K codestream.
 *
 * Splits the image into tiles of size @p tile_width × @p tile_height,
 * encodes each tile independently (as a separate tile-part with its
 * own SOT/SOD), and writes the combined codestream. Tiling enables
 * parallel encoding and random access to regions within the image.
 *
 * @param path Output file path.
 * @param image Source 8-bit image.
 * @param requested_levels DWT decomposition levels.
 * @param tile_width Tile width in pixels (≤ image width).
 * @param tile_height Tile height in pixels (≤ image height).
 * @param layers Number of quality layers (1–65535).
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_write_image_codestream_tiled(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int tile_width,
    int tile_height,
    uint16_t layers
);

/**
 * @brief Encode an image with ROI Maxshift to a raw J2K codestream.
 *
 * The rectangular image-domain ROI is traced through inverse 5-3 DWT
 * synthesis to identify contributing wavelet coefficients. Those
 * coefficients are magnitude-shifted by @p roi_shift bits so that
 * their most significant bit-planes appear in the highest-quality
 * layers. Uses the lossless 5-3/RCT path only.
 *
 * @param path Output file path.
 * @param image Source 8-bit image (lossless path only).
 * @param requested_levels DWT decomposition levels.
 * @param roi_rect Image-domain ROI rectangle in pixel coordinates.
 * @param roi_shift Number of bit-planes to shift ROI coefficients (RGN SPrgn).
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_write_image_codestream_roi(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    const dic_rect_i32 *roi_rect,
    uint8_t roi_shift
);

/**
 * @brief Encode an image to a JP2 file (single tile, 1 layer).
 *
 * Same encoding pipeline as j2k_write_image_codestream(), but wraps
 * the codestream in JP2 boxes (JP, FTYP, JP2H/IHDR/COLR, JP2C).
 *
 * @param path Output .jp2 file path.
 * @param image Source 8-bit image.
 * @param requested_levels DWT decomposition levels.
 * @param quality -1 for lossless, 1–100 for lossy.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_write_image_jp2(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int quality
);

/**
 * @brief Encode a tiled image to a JP2 file.
 *
 * Same as j2k_write_image_codestream_tiled() wrapped in JP2 boxes.
 *
 * @param path Output .jp2 file path.
 * @param image Source 8-bit image.
 * @param requested_levels DWT decomposition levels.
 * @param tile_width Tile width (≤ image width).
 * @param tile_height Tile height (≤ image height).
 * @param layers Quality layers (1–65535).
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_write_image_jp2_tiled(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int tile_width,
    int tile_height,
    uint16_t layers
);

/**
 * @brief Encode an ROI image to a JP2 file.
 *
 * Lossless 5-3/RCT path with Maxshift ROI. Wrapped in JP2 boxes.
 *
 * @param path Output .jp2 file path.
 * @param image Source 8-bit image.
 * @param requested_levels DWT levels.
 * @param roi_rect Image-domain ROI rectangle.
 * @param roi_shift ROI Maxshift scaling value.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_write_image_jp2_roi(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    const dic_rect_i32 *roi_rect,
    uint8_t roi_shift
);

/**
 * @brief Decode a raw J2K codestream into an 8-bit image.
 *
 * Equivalent to j2k_read_image_codestream_layers() with max_layers = 0
 * (decode all quality layers). Parses codestream markers, decodes
 * LRCP packets, reconstructs code-block coefficients via EBCOT
 * decoding, applies inverse DWT and inverse component transform,
 * and level-shifts back to unsigned 8-bit.
 *
 * The decoded image is owned by the caller and must be freed with
 * dic_image_u8_free().
 *
 * @param path Path to a .j2k codestream file.
 * @param image [out] Receives the decoded 8-bit image.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_read_image_codestream(
    const char *path,
    dic_image_u8 *image
);

/**
 * @brief Decode a J2K codestream with a quality-layer limit.
 *
 * Decodes only the first @p max_layers quality layers. When
 * @p max_layers is 0, decodes every layer signalled in the COD
 * marker. Nonzero values must be ≤ the signalled layer count.
 * The image is reconstructed from the cumulative bit-plane
 * contributions up to the specified layer.
 *
 * @param path Path to a .j2k file.
 * @param max_layers Number of quality layers to decode (0 = all).
 * @param image [out] Receives the reconstructed 8-bit image.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_read_image_codestream_layers(
    const char *path,
    uint16_t max_layers,
    dic_image_u8 *image
);

/**
 * @brief Decode a JP2 file into an 8-bit image.
 *
 * Locates the Contiguous Codestream (jp2c) box and decodes all
 * quality layers. Equivalent to j2k_read_image_jp2_layers()
 * with max_layers = 0.
 *
 * @param path Path to a .jp2 file.
 * @param image [out] Receives the decoded 8-bit image.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_read_image_jp2(
    const char *path,
    dic_image_u8 *image
);

/**
 * @brief Decode a JP2 file with a quality-layer limit.
 *
 * Locates the jp2c box and decodes the first @p max_layers quality
 * layers. max_layers = 0 decodes all layers.
 *
 * @param path Path to a .jp2 file.
 * @param max_layers Number of quality layers (0 = all).
 * @param image [out] Receives the reconstructed 8-bit image.
 * @return DIC_STATUS_OK on success.
 */
dic_status j2k_read_image_jp2_layers(
    const char *path,
    uint16_t max_layers,
    dic_image_u8 *image
);

#ifdef __cplusplus
}
#endif

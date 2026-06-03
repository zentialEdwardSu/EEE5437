/**
 * @file j2k_codestream.c
 * @brief Writes the JPEG 2000 core codestream marker stream defined by T.800 Annex A.
 *
 * This file implements the minimal SOC/SIZ/COD/RGN/QCD/SOT/SOD/EOC writer used by the
 * project tests and by the JP2 wrapper. It also assembles simple packet payloads produced
 * by the local EBCOT path. The implementation intentionally writes a constrained single-tile
 * codestream and does not attempt to expose every marker segment permitted by Annex A. When
 * SOP or EPH is requested through the COD Scod flags, callers are responsible for supplying
 * payload bytes that already contain those in-bit-stream markers at packet boundaries. The
 * writer also supports explicit COD maximum-precinct-size signalling while keeping packet
 * assembly constrained to the project's single-precinct layout. Multi-tile output is written
 * as one tile-part per tile by callers that provide per-tile payloads.
 *
 * References: j2k_codestream.h for public parameters, j2k_packet.h for Annex B
 * packet payload construction, jp2_file.c for Annex I file wrapping, and T.800 Annex J
 * examples for packet and arithmetic-decoder interoperability checks.
 */

#include "j2k/j2k_codestream.h"
#include "j2k/j2k_debug.h"

#include "j2k/j2k_packet.h"
#include "j2k/j2k_quant.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A.2-A.4, codestream marker syntax. */

/**
 * @brief Open a file for reading or writing, with MSVC compatibility.
 *
 * Uses fopen_s on MSVC (for secure CRT compliance) and standard fopen on
 * other platforms. This wrapper provides uniform error handling: returns
 * NULL on failure on all platforms.
 *
 * Callers are responsible for fclose() or equivalent cleanup.
 *
 * @param path  File path (non-NULL).
 * @param mode  File open mode string (e.g., "rb", "wb").
 * @return FILE* on success, NULL on failure.
 */
static FILE *j2k_open_file(const char *path, const char *mode)
{
    j2k_DEBUG_ENTER();
    FILE *file = NULL;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, mode) != 0)
        return NULL;
    return file;
#else
    return fopen(path, mode);
#endif
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A, marker segment byte-stream syntax. */

/**
 * @brief Write a single unsigned 8-bit byte to the codestream.
 *
 * All marker segment fields (Annex A.2) are byte-aligned, big-endian values.
 * This is the fundamental building block for all codestream output operations.
 * Returns 0 (EOF check) on write failure to detect I/O errors such as disk
 * full conditions.
 *
 * @param file   Open output file stream (non-NULL).
 * @param value  8-bit unsigned value to write.
 * @return 1 on success, 0 on write error (EOF).
 */
static int j2k_write_u8(FILE *file, uint8_t value)
{
    j2k_DEBUG_ENTER();
    return fputc((int)value, file) != EOF;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A tables list marker parameters as big-endian byte-stream fields. */

/**
 * @brief Write a 16-bit unsigned integer in big-endian (MSB-first) byte order.
 *
 * Per Annex A, all multi-byte marker segment fields are encoded most-significant
 * byte first (big-endian). This includes field lengths (Lxxx), image dimensions
 * (Xsiz, Ysiz per Table A.9), tile sizes, quantization step sizes (SPqcd per
 * Table A.29), and other marker parameters. Implementation splits the value by
 * masking and shifting (no endianness assumptions about the host platform) and
 * writes MSB then LSB.
 *
 * @param file   Open output file stream.
 * @param value  16-bit unsigned value in host byte order.
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_u16_be(FILE *file, uint16_t value)
{
    j2k_DEBUG_ENTER();
    return j2k_write_u8(file, (uint8_t)(value >> 8))
        && j2k_write_u8(file, (uint8_t)(value & 0xffu));
}

/* Reference: paper/T-REC-T.800-200208.pdf, Tables A.5 and A.9 define 32-bit Psot and geometry fields. */

/**
 * @brief Write a 32-bit unsigned integer in big-endian byte order.
 *
 * Per Annex A, 32-bit fields such as SIZ/Xsiz (reference grid width, Table A.9),
 * SIZ/Ysiz (reference grid height), SOT/Psot (tile-part length, Table A.5), and
 * JP2 box lengths (Annex I.5) are written as 4 big-endian bytes. Extracts four
 * bytes by successive shifts (24, 16, 8, 0 bits) and writes MSB first.
 *
 * @param file   Open output file stream.
 * @param value  32-bit unsigned value in host byte order.
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_u32_be(FILE *file, uint32_t value)
{
    j2k_DEBUG_ENTER();
    return j2k_write_u8(file, (uint8_t)(value >> 24))
        && j2k_write_u8(file, (uint8_t)((value >> 16) & 0xffu))
        && j2k_write_u8(file, (uint8_t)((value >> 8) & 0xffu))
        && j2k_write_u8(file, (uint8_t)(value & 0xffu));
}

/* Reference: paper/T-REC-T.800-200208.pdf, Table A.2, list of markers and marker segments. */

/**
 * @brief Write a 16-bit marker code to the codestream.
 *
 * Per Table A.2, markers are 16-bit codes in the range 0xFF00-0xFFFF, starting
 * with 0xFF followed by a marker identifier byte. Markers delimit the codestream
 * structure. Some markers are followed by a 16-bit length (marker segments such
 * as SIZ, COD, QCD), while others are standalone (SOC, SOD, EOC, SOP, EPH).
 *
 * Key marker codes used in this encoder (Table A.2):
 *   SOC (0xFF4F): Start of Codestream.    SOD (0xFF93): Start of Data.
 *   SIZ (0xFF51): Image and tile size.    EOC (0xFFD9): End of Codestream.
 *   COD (0xFF52): Coding style default.   QCD (0xFF5C): Quantization default.
 *   QCC (0xFF5D): Quantization component. RGN (0xFF5E): Region of interest.
 *   SOT (0xFF90): Start of tile-part.
 *
 * @param file    Open output file stream.
 * @param marker  16-bit marker code (0xFFxx).
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_marker(FILE *file, uint16_t marker)
{
    j2k_DEBUG_ENTER();
    return j2k_write_u16_be(file, marker);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.5.1, XTsiz/YTsiz define the regular tile grid over the reference grid. */

/**
 * @brief Compute the tile grid dimensions from codestream parameters.
 *
 * Per Annex A.5.1 Table A.9, the reference grid (Xsiz x Ysiz) is partitioned
 * into tiles of size XTsiz x YTsiz. The number of tiles in each direction is
 * ceil(Xsiz / XTsiz) and ceil(Ysiz / YTsiz). When XTsiz or YTsiz is 0, the
 * entire image is treated as a single tile. The total tile count must not
 * exceed 65536 (UINT16_MAX + 1) because the SOT/Isot field is uint16_t.
 *
 * @param params      Codestream parameters with width, height, tile dimensions.
 * @param tiles_x     [out] Number of tiles horizontally.
 * @param tiles_y     [out] Number of tiles vertically.
 * @param tile_count  [out] Total tiles (= tiles_x * tiles_y).
 * @return DIC_STATUS_OK or DIC_STATUS_INVALID_ARGUMENT.
 */
static dic_status j2k_tile_grid(
    const j2k_basic_params *params,
    uint32_t *tiles_x,
    uint32_t *tiles_y,
    uint32_t *tile_count
)
{
    j2k_DEBUG_ENTER();
    uint32_t tile_width;
    uint32_t tile_height;
    uint64_t count;

    if (params == NULL || tiles_x == NULL || tiles_y == NULL || tile_count == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    tile_width = params->tile_width == 0u ? params->width : params->tile_width;
    tile_height = params->tile_height == 0u ? params->height : params->tile_height;
    if (tile_width == 0u || tile_height == 0u || tile_width > params->width || tile_height > params->height)
        return DIC_STATUS_INVALID_ARGUMENT;

    *tiles_x = (params->width + tile_width - 1u) / tile_width;
    *tiles_y = (params->height + tile_height - 1u) / tile_height;
    count = (uint64_t)(*tiles_x) * (uint64_t)(*tiles_y);
    if (count == 0u || count > UINT16_MAX + 1u)
        return DIC_STATUS_INVALID_ARGUMENT;

    *tile_count = (uint32_t)count;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.1 Table A.21, this encoder only supports the explicit 15/15 maximum precinct that preserves the single-precinct packet layout. */

/**
 * @brief Validate precinct size parameters, enforcing 15/15 maximum only.
 *
 * Per Annex A.6.1 Table A.21, the COD marker's SPcod precinct size parameters
 * specify precinct width/height exponents (PPx, PPy) for each resolution level.
 * Valid values are 0-15 (2^PPx gives actual precinct size). This encoder only
 * supports PPx=PPy=15 (the maximum), collapsing each resolution to a single
 * precinct and simplifying the LRCP packet layout.
 *
 * @param params  Codestream parameters.
 * @return DIC_STATUS_OK if valid, DIC_J2K_UNSUPPORTED_PRECINCT_SIZE or error.
 */
static dic_status j2k_validate_precincts(const j2k_basic_params *params)
{
    j2k_DEBUG_ENTER();
    uint8_t resolution;

    if (!params->use_precincts)
        return DIC_STATUS_OK;

    /** Check each resolution level r=0..N_L: PPx and PPy are 4-bit fields.
     *  Only the maximum value 15 is allowed in this encoder. */
    for (resolution = 0u; resolution <= params->decomposition_levels; ++resolution)
    {
        uint8_t ppx = params->precinct_width_exponents[resolution];
        uint8_t ppy = params->precinct_height_exponents[resolution];

        if (ppx > 15u || ppy > 15u)
            return DIC_STATUS_INVALID_ARGUMENT;
        if (ppx != 15u || ppy != 15u)
            return DIC_J2K_UNSUPPORTED_PRECINCT_SIZE;
    }

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.3, SOD begins the tile-part bit-stream data. */

/**
 * @brief Write a block of raw bytes to the codestream.
 *
 * Used for the packet data portion (between SOD and EOC markers), containing
 * SOP/EPH-delimited packet headers and code-block codeword bytes per Annex B.10.
 * Uses fwrite for bulk I/O efficiency. Returns success (1) when size is 0.
 *
 * @param file         Open output file stream.
 * @param payload      Raw bytes pointer (may be NULL only if size == 0).
 * @param payload_size Number of bytes to write.
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_bytes(FILE *file, const uint8_t *payload, size_t payload_size)
{
    j2k_DEBUG_ENTER();
    if (payload_size == 0u)
        return 1;
    return fwrite(payload, 1u, payload_size, file) == payload_size;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.5.1 and Table A.9, Image and tile size marker segment. */

/**
 * @brief Write the SIZ marker segment per Annex A.5.1 Table A.9.
 *
 * SIZ describes the fundamental geometry of the codestream. Field layout:
 *   SIZ (0xFF51)   Lsiz=38+3*Csiz   Rsiz=0 (baseline)
 *   Xsiz           Ysiz             XOsiz=0       YOsiz=0
 *   XTsiz          YTsiz            XTOsiz=0      YTOsiz=0
 *   Csiz (1 or 3)  per-component: SSiz=7 (8-bit unsigned), XRsiz=1, YRsiz=1
 *
 * Tile dimensions default to reference grid when no tiling is active.
 * This encoder only supports 8-bit unsigned (SSiz=7), no subsampling (XRsiz=YRsiz=1).
 *
 * @param file    Open output file stream.
 * @param params  Codestream parameters.
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_siz(FILE *file, const j2k_basic_params *params)
{
    j2k_DEBUG_ENTER();
    uint16_t component;
    /** Lsiz = 38 + 3*Csiz per Table A.9: 38 basic bytes + 3 per component. */
    uint16_t length = (uint16_t)(38u + (uint16_t)params->components * 3u);
    /** Tile size defaults to reference grid size when 0 (single tile). */
    uint32_t tile_width = params->tile_width == 0u ? params->width : params->tile_width;
    uint32_t tile_height = params->tile_height == 0u ? params->height : params->tile_height;

    if (!j2k_write_marker(file, j2k_MARKER_SIZ)
        || !j2k_write_u16_be(file, length)
        || !j2k_write_u16_be(file, 0u)         /** Rsiz=0: baseline profile, no capabilities. */
        || !j2k_write_u32_be(file, params->width)   /** Xsiz: reference grid width. */
        || !j2k_write_u32_be(file, params->height)  /** Ysiz: reference grid height. */
        || !j2k_write_u32_be(file, 0u)         /** XOsiz: horizontal grid offset. */
        || !j2k_write_u32_be(file, 0u)         /** YOsiz: vertical grid offset. */
        || !j2k_write_u32_be(file, tile_width)      /** XTsiz: tile reference width. */
        || !j2k_write_u32_be(file, tile_height)     /** YTsiz: tile reference height. */
        || !j2k_write_u32_be(file, 0u)         /** XTOsiz: tile horizontal offset. */
        || !j2k_write_u32_be(file, 0u)         /** YTOsiz: tile vertical offset. */
        || !j2k_write_u16_be(file, params->components)) /** Csiz: component count. */
    {
        return 0;
    }

    /** Per-component parameters per Table A.10: SSiz=7 => 8-bit unsigned. */
    for (component = 0; component < params->components; ++component)
    {
        if (!j2k_write_u8(file, 7u)    /** SSiz=7: depth=8 bits, unsigned. */
            || !j2k_write_u8(file, 1u) /** XRsiz=1: no horizontal subsampling. */
            || !j2k_write_u8(file, 1u))/** YRsiz=1: no vertical subsampling. */
        {
            return 0;
        }
    }

    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.1, Figure A.9 and Tables A.12-A.20, Coding style default syntax. */

/**
 * @brief Write the COD marker segment per Annex A.6.1 Tables A.12-A.21.
 *
 * Scod bit flags (Table A.13): bit 0=precincts, 1=SOP, 2=EPH.
 * SPcod fields per Tables A.15-A.21: progression=0 (LRCP), layers, MCT flag,
 * decomposition levels NL, code-block exponent=4 (=> 2^6 = 64), code-block style
 * 0x04 (bypass + causal + regular termination per Annex D.5.2), transform=0/1
 * (9-7 irreversible / 5-3 reversible). When precincts are used, PPx/PPy are
 * packed per resolution: PPy<<4 | PPx (Table A.21).
 *
 * @param file    Open output file stream.
 * @param params  Codestream parameters.
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_cod(FILE *file, const j2k_basic_params *params)
{
    j2k_DEBUG_ENTER();
    uint8_t scod = 0u;
    /** Lcod = 12 + (NL+1) when precincts present, else 12. */
    uint16_t length = (uint16_t)(12u + (params->use_precincts ? (uint16_t)params->decomposition_levels + 1u : 0u));
    uint8_t resolution;

    /** Build Scod flags per Table A.13. */
    if (params->use_precincts)
        scod |= 0x01u; /** Bit 0: precinct parameters present. */
    if (params->use_sop)
        scod |= 0x02u; /** Bit 1: SOP markers in bit-stream. */
    if (params->use_eph)
        scod |= 0x04u; /** Bit 2: EPH markers in bit-stream. */

    if (!j2k_write_marker(file, j2k_MARKER_COD)
        || !j2k_write_u16_be(file, length)
        || !j2k_write_u8(file, scod)           /** Scod: coding style flags. */
        || !j2k_write_u8(file, 0u)             /** Progression order=0: LRCP (Table A.15). */
        || !j2k_write_u16_be(file, params->layers == 0u ? 1u : params->layers) /** Layers (Table A.16). */
        || !j2k_write_u8(file, params->multiple_component_transform ? 1u : 0u) /** MCT flag (Table A.17). */
        || !j2k_write_u8(file, params->decomposition_levels) /** NL: DWT levels (Table A.18). */
        || !j2k_write_u8(file, 4u)             /** Code-block width exponent 4 => 2^(4+2)=64 (Table A.19). */
        || !j2k_write_u8(file, 4u)             /** Code-block height exponent 4 => 64 (Table A.19). */
        || !j2k_write_u8(file, 0x04u)          /** Code-block style 0x04: bypass+causal+regular (Table A.20, Annex D.5.2). */
        || !j2k_write_u8(file, params->reversible ? 1u : 0u)) /** Transform: 0=9-7, 1=5-3 (Table A.21). */
    {
        return 0;
    }

    /** Precinct size bytes: PPy (bits 7-4) | PPx (bits 3-0) for r=0..N_L. */
    for (resolution = 0u; params->use_precincts && resolution <= params->decomposition_levels; ++resolution)
    {
        uint8_t precinct = (uint8_t)(
            (uint8_t)(params->precinct_height_exponents[resolution] << 4u)
            | params->precinct_width_exponents[resolution]
        );

        if (!j2k_write_u8(file, precinct))
            return 0;
    }

    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.3 Tables A.24-A.26, RGN signals implicit Maxshift ROI scaling. */

/**
 * @brief Write the RGN marker segment per Annex A.6.3.
 *
 * Signals implicit Maxshift ROI coding (Srgn=0, Annex H.2). Writes one RGN
 * per component when roi_shift > 0. Fields: RGN 0xFF5E, Lrgn=5, Crgn=component,
 * Srgn=0 (implicit Maxshift), SPrgn=roi_shift.
 *
 * @param file    Open output file stream.
 * @param params  Codestream parameters.
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_rgn(FILE *file, const j2k_basic_params *params)
{
    j2k_DEBUG_ENTER();
    uint16_t component;

    if (params->roi_shift == 0u)
        return 1;

    for (component = 0u; component < params->components; ++component)
    {
        if (!j2k_write_marker(file, j2k_MARKER_RGN)
            || !j2k_write_u16_be(file, 5u)    /** Lrgn=5 (fixed length). */
            || !j2k_write_u8(file, component)  /** Crgn: component index (Table A.24). */
            || !j2k_write_u8(file, 0u)         /** Srgn=0: implicit Maxshift (Table A.25, Annex H.2). */
            || !j2k_write_u8(file, params->roi_shift)) /** SPrgn: ROI shift exponent (Table A.26). */
        {
            return 0;
        }
    }

    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.4 Table A.29 and E.1.1 Table E.1, reversible SPqcd stores the exponent in the high five bits. */

/**
 * @brief Compute the reversible QCD/QCC SPqcd byte for a sub-band.
 *
 * Per Annex E.1.1 Table E.1, the 5-3 DWT synthesis filter gains produce
 * dynamic range expansion: gain_log2=0 for LL, 1 for HL/LH, 2 for HH.
 * The exponent is 8 + gain_log2 + component_extra_bits, placed in the upper
 * 5 bits of the byte (<< 3). The component_extra_bits parameter (0 for luma,
 * 1 for chroma RCT) gives chroma channels one extra bit-plane.
 *
 * @param subband_in_level    0=HL, 1=LH, 2=HH, >=3=LL.
 * @param component_extra_bits Chroma extra bits (0 or 1).
 * @return 8-bit SPqcd with exponent in upper 5 bits.
 */
static uint8_t j2k_reversible_qcd_spqcd(unsigned int subband_in_level, uint8_t component_extra_bits)
{
    j2k_DEBUG_ENTER();
    /** Synthesis gain log2: HL=1, LH=1, HH=2 (Table E.1). LL (index>=3) gets 0. */
    static const uint8_t gain_log2_by_subband[] = {1u, 1u, 2u};
    uint8_t exponent;

    if (subband_in_level >= 3u)
        exponent = (uint8_t)(8u + component_extra_bits);
    else
        exponent = (uint8_t)(8u + component_extra_bits + gain_log2_by_subband[subband_in_level]);
    /** Place exponent in upper 5 bits; lower 3 bits are zero. */
    return (uint8_t)(exponent << 3u);
}

/**
 * @brief Determine mantissa bits (epsilon_b) for irreversible SPqcd encoding.
 *
 * Per Annex A.6.4 Tables A.27-A.28 and Annex E.1.2: LL uses epsilon_b=8,
 * HL/LH use 9, HH uses 10. HH sub-bands are identified by (step_index-1)%3 == 2
 * because QCD sub-band order within a level is HL(0), LH(1), HH(2).
 *
 * @param step_index  Position in QCD step array (0=LL, then HL/LH/HH per level).
 * @return Number of mantissa bits: 8, 9, or 10.
 */
static unsigned int j2k_irreversible_qcd_range_bits(unsigned int step_index)
{
    if (step_index == 0u)
        return 8u; /** LL sub-band: 8 mantissa bits. */
    /** For sub-bands in resolution levels r>0: (idx-1)%3 gives 0=HL, 1=LH, 2=HH. */
    return ((step_index - 1u) % 3u) == 2u ? 10u : 9u;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.4 Tables A.27-A.29 and E.2 Equation E-10, reversible QCD syntax. */

/**
 * @brief Write the QCD marker segment per Annex A.6.4 Tables A.27-A.29.
 *
 * Two formats: reversible (Sqcd format 0x40, 1-byte SPqcd per sub-band) and
 * irreversible (Sqcd format 0x02 with guard_bits in upper 3 bits, 2-byte SPqcd
 * per sub-band with (epsilon_b, mu_b) encoding per Annex E.1.2 Equation E-10).
 *
 * Sub-band count = 1 (LL) + 3*NL (HL, LH, HH at each level). For reversible,
 * SPqcd stores only the exponent in upper 5 bits. For irreversible, SPqcd is
 * a 16-bit (epsilon_b, mu_b) pair encoding the step size.
 *
 * @param file    Open output file stream.
 * @param params  Codestream parameters.
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_qcd(FILE *file, const j2k_basic_params *params)
{
    j2k_DEBUG_ENTER();
    unsigned int level;
    /** Total sub-bands: 1 LL + 3*NL (HL, LH, HH per level). */
    unsigned int step_count = 1u + 3u * (unsigned int)params->decomposition_levels;

    if (!params->reversible)
    {
        /** Irreversible format (Tables A.27-A.28):
         *  Sqcd format: GGGGGGTT where G=guard_bits (0-7), TT=2 (irreversible).
         *  Length: 3 + 2*step_count (2-byte SPqcd each).
         *  Each SPqcd encodes (epsilon_b, mu_b) per Annex E.1.2 Equation E-10. */
        unsigned int step;
        uint16_t length = (uint16_t)(3u + 2u * step_count);

        if (params->quant_step_count != step_count || params->quant_guard_bits > 7u)
            return 0;
        if (!j2k_write_marker(file, j2k_MARKER_QCD)
            || !j2k_write_u16_be(file, length)
            /** Sqcd: upper 3 bits = guard_bits, lower 5 = 0x02 (irreversible, Table A.28). */
            || !j2k_write_u8(file, (uint8_t)((params->quant_guard_bits << 5u) | 0x02u)))
        {
            return 0;
        }
        for (step = 0u; step < step_count; ++step)
        {
            uint16_t spqcd;

            /** Encode double step_size to 16-bit SPqcd with epsilon_b mantissa bits. */
            if (j2k_quant_encode_irreversible_spqcd(
                    params->quant_step_sizes[step],
                    j2k_irreversible_qcd_range_bits(step),
                    &spqcd
                ) != DIC_STATUS_OK
                || !j2k_write_u16_be(file, spqcd))
            {
                return 0;
            }
        }
        return 1;
    }

    /** Reversible format (Table A.29):
     *  Sqcd = 0x40 (guard_bits=2, reversible format=0).
     *  Length: 4 + 3*NL (1-byte LL + 3 bytes per level).
     *  Each SPqcd byte has exponent in upper 5 bits, lower 3 bits zero. */
    if (!j2k_write_marker(file, j2k_MARKER_QCD)
        || !j2k_write_u16_be(file, (uint16_t)(4u + 3u * (unsigned int)params->decomposition_levels))
        || !j2k_write_u8(file, 0x40u)            /** Sqcd=0x40: guard_bits=2, reversible. */
        || !j2k_write_u8(file, j2k_reversible_qcd_spqcd(3u, 0u))) /** LL SPqcd, index>=3. */
    {
        return 0;
    }

    /** For each level, write HL(0), LH(1), HH(2) SPqcd bytes. */
    for (level = 0u; level < params->decomposition_levels; ++level)
    {
        if (!j2k_write_u8(file, j2k_reversible_qcd_spqcd(0u, 0u))
            || !j2k_write_u8(file, j2k_reversible_qcd_spqcd(1u, 0u))
            || !j2k_write_u8(file, j2k_reversible_qcd_spqcd(2u, 0u)))
        {
            return 0;
        }
    }

    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.5 and E.2 Equation E-10, RCT chroma components signal one additional bit of reversible precision with QCC. */

/**
 * @brief Write the QCC marker segment per Annex A.6.5.
 *
 * Per-component quantization override for chroma channels (Cb=1, Cr=2) when
 * RCT is active. Component_extra_bits=1 gives chroma one extra bit-plane of
 * reversible precision. Fields: QCC 0xFF5D, Lqcc=5+3*NL, Cqcc=component,
 * Sqcc=0x40 (reversible), SPqcc bytes for LL then HL/LH/HH per level.
 *
 * @param file                Open output file stream.
 * @param params              Codestream parameters.
 * @param component           0-based component index (1 or 2 for chroma).
 * @param component_extra_bits Extra bit-planes (1 for chroma).
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_qcc(
    FILE *file,
    const j2k_basic_params *params,
    uint16_t component,
    uint8_t component_extra_bits
)
{
    j2k_DEBUG_ENTER();
    unsigned int level;
    /** Lqcc = 5 (marker+length+Cqcc+Sqcc) + 1*LL + 3*NL. */
    uint16_t length = (uint16_t)(5u + 3u * (unsigned int)params->decomposition_levels);

    if (params->components > 256u)
        return 0;
    if (!j2k_write_marker(file, j2k_MARKER_QCC)
        || !j2k_write_u16_be(file, length)
        || !j2k_write_u8(file, (uint8_t)component)  /** Cqcc: component index (Table A.31). */
        || !j2k_write_u8(file, 0x40u)              /** Sqcc=0x40: reversible, guard_bits=2. */
        || !j2k_write_u8(file, j2k_reversible_qcd_spqcd(3u, component_extra_bits)))
    {
        return 0;
    }

    for (level = 0u; level < params->decomposition_levels; ++level)
    {
        if (!j2k_write_u8(file, j2k_reversible_qcd_spqcd(0u, component_extra_bits))
            || !j2k_write_u8(file, j2k_reversible_qcd_spqcd(1u, component_extra_bits))
            || !j2k_write_u8(file, j2k_reversible_qcd_spqcd(2u, component_extra_bits)))
        {
            return 0;
        }
    }

    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.2 and Table A.5, Start of tile-part syntax and Psot. */

/**
 * @brief Write the SOT marker segment per Annex A.4.2 Table A.5.
 *
 * SOT signals a tile-part start. Psot = payload_size + 14 accounts for:
 * 2(SOT) + 2(Lsot) + 2(Isot) + 4(Psot) + 1(TPsot) + 1(TNsot) + 2(SOD) = 14.
 * This enables decoders to skip to the next tile-part by seeking Psot bytes.
 *
 * @param file             Open output file stream.
 * @param tile_index       0-based tile index (Isot, 16-bit).
 * @param payload_size     Data bytes following SOD.
 * @param tile_part_index  Index of this tile-part (TPsot).
 * @param tile_part_count  Total tile-parts for this tile (TNsot).
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_sot(
    FILE *file,
    uint16_t tile_index,
    size_t payload_size,
    uint8_t tile_part_index,
    uint8_t tile_part_count
)
{
    j2k_DEBUG_ENTER();
    uint32_t tile_part_length;

    /** Psot = payload_size + 14 (SOT fields + SOD marker). Must fit in uint32. */
    if (payload_size > UINT32_MAX - 14u)
        return 0;

    tile_part_length = (uint32_t)payload_size + 14u;
    return j2k_write_marker(file, j2k_MARKER_SOT)
        && j2k_write_u16_be(file, 10u)              /** Lsot=10 (fixed length). */
        && j2k_write_u16_be(file, tile_index)        /** Isot: tile index. */
        && j2k_write_u32_be(file, tile_part_length)  /** Psot: total tile-part length. */
        && j2k_write_u8(file, tile_part_index)        /** TPsot: tile-part index. */
        && j2k_write_u8(file, tile_part_count);       /** TNsot: total tile-parts for this tile. */
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.6, main header marker segments precede every tile-part. */

/**
 * @brief Write the complete codestream main header.
 *
 * Order per Annex A.3: SOC -> SIZ -> COD -> QCD -> (optional QCC for
 * chroma when RCT is active) -> (optional RGN when roi_shift > 0).
 * QCC writes component_extra_bits=1 for Cb(1) and Cr(2) to signal
 * the RCT chroma dynamic range expansion of 1 bit-plane.
 *
 * @param file    Open output file stream.
 * @param params  Codestream parameters.
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_main_header(FILE *file, const j2k_basic_params *params)
{
    j2k_DEBUG_ENTER();
    return j2k_write_marker(file, j2k_MARKER_SOC)
        && j2k_write_siz(file, params)
        && j2k_write_cod(file, params)
        && j2k_write_qcd(file, params)
        /** When RCT is active (reversible + MCT), write QCC for chroma
         *  components 1 (Cb) and 2 (Cr) with 1 extra bit-plane. */
        && (params->reversible && params->multiple_component_transform
            ? (j2k_write_qcc(file, params, 1u, 1u)
                && j2k_write_qcc(file, params, 2u, 1u))
            : 1)
        && j2k_write_rgn(file, params);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.2-A.4.3, a tile-part is SOT, SOD, then compressed data. */

/**
 * @brief Write a single tile-part: SOT, SOD, then compressed data bytes.
 *
 * Per Annex A.4.2-A.4.3: SOT marker (tile metadata), SOD marker (start of
 * data), then the packet bit-stream (Annex B.10). No tile-part header marker
 * segments are inserted (PPM/PPT not used by this encoder).
 *
 * @param file       Open output file stream.
 * @param tile_part  Tile-part descriptor (index, lengths, payload).
 * @return 1 on success, 0 on write error.
 */
static int j2k_write_tile_part(FILE *file, const j2k_tile_part_payload *tile_part)
{
    j2k_DEBUG_ENTER();
    return j2k_write_sot(
            file,
            tile_part->tile_index,
            tile_part->payload_size,
            tile_part->tile_part_index,
            tile_part->tile_part_count
        )
        && j2k_write_marker(file, j2k_MARKER_SOD)
        && j2k_write_bytes(file, tile_part->payload, tile_part->payload_size);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3 and Figures A.3-A.5, codestream and tile-part construction. */

/**
 * @brief Write a minimal single-tile codestream with empty packets to a file.
 *
 * Convenience wrapper for testing: writes SOC/SIZ/COD/QCD main header,
 * one SOT/SOD tile-part with empty LRCP packets, and EOC. Delegates to
 * j2k_write_empty_packet_codestream().
 *
 * @param path    Output file path.
 * @param params  Codestream parameters.
 * @return DIC_STATUS_OK or error code.
 */
dic_status j2k_write_minimal_codestream(
    const char *path,
    const j2k_basic_params *params
)
{
    j2k_DEBUG_ENTER();
    return j2k_write_empty_packet_codestream(path, params);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.2-A.4.4, SOT/Psot, SOD data, and EOC syntax. */

/**
 * @brief Write a single-tile codestream with pre-built packet payload to a file.
 *
 * Primary codestream output path for single-tile images. Wraps the caller's
 * packet data in a complete codestream: SOC, main header, SOT(0)/SOD, payload, EOC.
 *
 * @param path          Output file path.
 * @param params        Codestream parameters for main header.
 * @param payload       Pre-built packet data (SOP/EPH + code-block bytes).
 * @param payload_size  Payload byte count.
 * @return DIC_STATUS_OK or error code.
 */
dic_status j2k_write_codestream_with_payload(
    const char *path,
    const j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
)
{
    j2k_DEBUG_ENTER();
    j2k_tile_part_payload tile_part;

    if (path == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (payload_size > 0u && payload == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    /** Single tile (index 0), single tile-part. */
    tile_part.tile_index = 0u;
    tile_part.tile_part_index = 0u;
    tile_part.tile_part_count = 1u;
    tile_part.payload = payload;
    tile_part.payload_size = payload_size;
    return j2k_write_codestream_with_tile_parts(path, params, &tile_part, 1u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5.2.1, JP2 Contiguous Codestream box embeds a codestream byte stream. */

/**
 * @brief Write a minimal empty-packet codestream to an open stream.
 *
 * Stream variant for JP2 wrapping (Annex I.5.2.1). Used by jp2_file.c to
 * embed the codestream into a Contiguous Codestream box (jp2c) without an
 * intermediate file.
 *
 * @param file    Open output file stream.
 * @param params  Codestream parameters.
 * @return DIC_STATUS_OK or error code.
 */
dic_status j2k_write_minimal_codestream_stream(
    FILE *file,
    const j2k_basic_params *params
)
{
    j2k_DEBUG_ENTER();
    return j2k_write_empty_packet_codestream_stream(file, params);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.4, main header followed by one tile-part and EOC. */

/**
 * @brief Write a single-tile codestream with payload to an open stream.
 *
 * Stream variant of j2k_write_codestream_with_payload(). Used by JP2 writing
 * to embed codestream in a jp2c box. Avoids double-buffering to temporary file.
 *
 * @param file          Open output file stream.
 * @param params        Codestream parameters.
 * @param payload       Packet data.
 * @param payload_size  Payload byte count.
 * @return DIC_STATUS_OK or error code.
 */
dic_status j2k_write_codestream_with_payload_stream(
    FILE *file,
    const j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
)
{
    j2k_DEBUG_ENTER();
    j2k_tile_part_payload tile_part;

    if (file == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (payload_size > 0u && payload == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    tile_part.tile_index = 0u;
    tile_part.tile_part_index = 0u;
    tile_part.tile_part_count = 1u;
    tile_part.payload = payload;
    tile_part.payload_size = payload_size;
    return j2k_write_codestream_with_tile_parts_stream(file, params, &tile_part, 1u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.4, a codestream may contain multiple SOT/SOD tile-parts after the main header. */

/**
 * @brief Write a multi-tile codestream to a file.
 *
 * Opens file, writes: SOC, main header, then SOT/SOD/payload per tile-part,
 * then EOC. Delegates core writing to j2k_write_codestream_with_tile_parts_stream().
 * Ensures fclose errors propagate even if prior writes succeeded.
 *
 * @param path              Output file path.
 * @param params            Main header parameters.
 * @param tile_parts        Array of tile-part descriptors.
 * @param tile_part_count   Number of tile-parts.
 * @return DIC_STATUS_OK or error code.
 */
dic_status j2k_write_codestream_with_tile_parts(
    const char *path,
    const j2k_basic_params *params,
    const j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
)
{
    j2k_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status;

    if (path == NULL || params == NULL || tile_parts == NULL || tile_part_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = j2k_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    status = j2k_write_codestream_with_tile_parts_stream(file, params, tile_parts, tile_part_count);
    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.2 Table A.5, each SOT carries its tile index, tile-part index, and tile-part count. */

/**
 * @brief Core codestream writer: writes main header, all tile-parts, and EOC to a stream.
 *
 * Performs thorough pre-write validation: image dimensions, component count,
 * MCT consistency, tile bounds, NL range, precinct sizes, tile grid validity,
 * tile-part count sufficiency, and per-tile-part field correctness.
 *
 * @param file              Open output file stream.
 * @param params            Codestream parameters.
 * @param tile_parts        Tile-part descriptors.
 * @param tile_part_count   Number of tile-parts.
 * @return DIC_STATUS_OK or error code.
 */
dic_status j2k_write_codestream_with_tile_parts_stream(
    FILE *file,
    const j2k_basic_params *params,
    const j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
)
{
    j2k_DEBUG_ENTER();
    uint32_t tiles_x;
    uint32_t tiles_y;
    uint32_t expected_tiles;
    size_t index;
    uint8_t *seen_tiles = NULL;
    dic_status status;

    if (file == NULL || params == NULL || tile_parts == NULL || tile_part_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    /** Image dimensions must be positive (Annex A.5.1, Table A.9). */
    if (params->width == 0u || params->height == 0u)
        return DIC_J2K_INVALID_DIMENSIONS;
    /** Only 1 (grey) or 3 (RGB) components supported. */
    if (params->components != 1u && params->components != 3u)
        return DIC_J2K_INVALID_COMPONENTS;
    /** MCT requires exactly 3 components (Annex G). */
    if (params->multiple_component_transform && params->components != 3u)
        return DIC_STATUS_INVALID_ARGUMENT;
    /** Tile dimensions must fit within the reference grid. */
    if ((params->tile_width != 0u && params->tile_width > params->width)
        || (params->tile_height != 0u && params->tile_height > params->height))
    {
        return DIC_STATUS_INVALID_ARGUMENT;
    }
    /** NL must be within the valid range per Annex A.6.1 Table A.18 (0..32). */
    if (params->decomposition_levels > j2k_MAX_DECOMPOSITION_LEVELS)
        return DIC_J2K_INVALID_LEVELS;
    {
        /** Verify precinct size parameters before writing COD. */
        dic_status precinct_status = j2k_validate_precincts(params);

        if (precinct_status != DIC_STATUS_OK)
            return precinct_status;
    }
    /** Compute tile grid dimensions for validation. */
    status = j2k_tile_grid(params, &tiles_x, &tiles_y, &expected_tiles);
    if (status != DIC_STATUS_OK)
        return status;
    (void)tiles_x;
    (void)tiles_y;

    /** Must have >= 1 tile-part per tile. */
    if (tile_part_count < expected_tiles)
        return DIC_STATUS_INVALID_ARGUMENT;
    /** Track which tiles have a first tile-part (index 0). */
    seen_tiles = (uint8_t *)calloc(expected_tiles, sizeof(seen_tiles[0]));
    if (seen_tiles == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    /** Validate each tile-part descriptor. */
    for (index = 0u; index < tile_part_count; ++index)
    {
        /** Tile index must be valid (within grid). */
        if (tile_parts[index].tile_index >= expected_tiles)
        {
            free(seen_tiles);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
        /** TPsot < TNsot per Table A.5. */
        if (tile_parts[index].tile_part_count != 0u
            && tile_parts[index].tile_part_index >= tile_parts[index].tile_part_count)
        {
            free(seen_tiles);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
        /** Payload pointer must be valid when size > 0. */
        if (tile_parts[index].payload_size > 0u && tile_parts[index].payload == NULL)
        {
            free(seen_tiles);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
        /** Psot must fit in 32 bits. */
        if (tile_parts[index].payload_size > UINT32_MAX - 14u)
        {
            free(seen_tiles);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
        /** Mark tile as having first tile-part. */
        if (tile_parts[index].tile_part_index == 0u)
            seen_tiles[tile_parts[index].tile_index] = 1u;
    }
    /** Every tile must have a first tile-part. */
    for (index = 0u; index < expected_tiles; ++index)
    {
        if (!seen_tiles[index])
        {
            free(seen_tiles);
            return DIC_STATUS_INVALID_ARGUMENT;
        }
    }
    free(seen_tiles);

    /** --- Writing Phase ---
     *  1. Main Header: SOC, SIZ, COD, QCD, optional QCC/RGN.
     *  2. Tile-parts: SOT, SOD, payload data.
     *  3. EOC terminator.
     */
    if (!j2k_write_main_header(file, params))
    {
        return DIC_STATUS_IO_ERROR;
    }

    for (index = 0u; index < tile_part_count; ++index)
    {
        if (!j2k_write_tile_part(file, tile_parts + index))
            return DIC_STATUS_IO_ERROR;
    }

    if (!j2k_write_marker(file, j2k_MARKER_EOC))
        return DIC_STATUS_IO_ERROR;

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.3 and B.10.8, empty packets still appear in LRCP packet order. */

/**
 * @brief Write a codestream with all-empty packets to a file.
 *
 * Creates empty packets for every (layer, resolution, component, tile) slot.
 * Per Annex B.10.3, empty packets still appear in LRCP order with a single
 * zero bit in the header (nonempty=0) padded to a byte boundary. Used for
 * testing the codestream structure without actual entropy-coded data.
 *
 * @param path    Output file path.
 * @param params  Codestream parameters.
 * @return DIC_STATUS_OK or error code.
 */
dic_status j2k_write_empty_packet_codestream(
    const char *path,
    const j2k_basic_params *params
)
{
    j2k_DEBUG_ENTER();
    FILE *file = NULL;
    dic_status status;

    if (path == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = j2k_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    status = j2k_write_empty_packet_codestream_stream(file, params);
    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.1 and B.10.3, an empty packet is encoded by a zero first header bit padded to a byte. */

/**
 * @brief Write a codestream with all-empty packets to an open stream.
 *
 * Builds empty LRCP packets via j2k_packet_build_empty_lrcp_payload() and
 * writes the codestream. Each empty packet per Annex B.10.1 has one header
 * bit (nonempty=0) padded to a byte. All tiles share the same empty payload.
 *
 * @param file    Open output file stream.
 * @param params  Codestream parameters.
 * @return DIC_STATUS_OK or error code.
 */
dic_status j2k_write_empty_packet_codestream_stream(
    FILE *file,
    const j2k_basic_params *params
)
{
    j2k_DEBUG_ENTER();
    j2k_packet_header payload;
    j2k_tile_part_payload *tile_parts = NULL;
    uint32_t tiles_x;
    uint32_t tiles_y;
    uint32_t tile_count;
    uint32_t tile_index;
    dic_status status;

    if (file == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    j2k_packet_header_init(&payload);
    /** Compute tile grid; each tile gets identical empty LRCP packets. */
    status = j2k_tile_grid(params, &tiles_x, &tiles_y, &tile_count);
    if (status != DIC_STATUS_OK)
    {
        j2k_packet_header_free(&payload);
        return status;
    }
    (void)tiles_x;
    (void)tiles_y;

    /** Build empty LRCP payload: one zero-bit packet per (layer, res, component). */
    status = j2k_packet_build_empty_lrcp_payload(
        params->components,
        params->decomposition_levels,
        params->layers,
        &payload
    );
    if (status == DIC_STATUS_OK)
    {
        tile_parts = (j2k_tile_part_payload *)calloc(tile_count, sizeof(tile_parts[0]));
        if (tile_parts == NULL)
        {
            status = DIC_STATUS_MEMORY_ERROR;
        }
    }
    /** Assign same empty payload to each tile-part. */
    for (tile_index = 0u; status == DIC_STATUS_OK && tile_index < tile_count; ++tile_index)
    {
        tile_parts[tile_index].tile_index = (uint16_t)tile_index;
        tile_parts[tile_index].tile_part_index = 0u;
        tile_parts[tile_index].tile_part_count = 1u;
        tile_parts[tile_index].payload = payload.data;
        tile_parts[tile_index].payload_size = payload.size;
    }
    if (status == DIC_STATUS_OK)
    {
        status = j2k_write_codestream_with_tile_parts_stream(file, params, tile_parts, tile_count);
    }
    free(tile_parts);
    j2k_packet_header_free(&payload);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.3 and B.10.8, SOD carries packet headers followed by EBCOT code-block bytes. */

/**
 * @brief Write a single-tile codestream from EBCOT code-block streams to a file.
 *
 * Builds one LRCP packet from the given streams (MQ codewords per Annex C,
 * EBCOT coding pass data per Annex D), then writes the codestream. Used for
 * unit testing EBCOT encoding. Note: produces exactly one packet with one
 * layer of contributions; multi-layer/resolution assembly uses j2k_image_build_payload().
 *
 * @param path          Output file path.
 * @param params        Codestream parameters.
 * @param streams       EBCOT code-block streams.
 * @param stream_count  Number of streams.
 * @return DIC_STATUS_OK or error code.
 */
dic_status j2k_write_ebcot_packet_codestream(
    const char *path,
    const j2k_basic_params *params,
    const j2k_codeblock_stream *streams,
    size_t stream_count
)
{
    j2k_DEBUG_ENTER();
    FILE *file;
    dic_status status;

    if (path == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = j2k_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;

    status = j2k_write_ebcot_packet_codestream_stream(file, params, streams, stream_count);
    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.8, packet payloads are generated in code-block scan order for the current packet. */

/**
 * @brief Write a single-tile codestream from EBCOT streams to an open stream.
 *
 * Builds a packet from code-block streams via j2k_packet_build_ebcot_payload()
 * (assembling inclusion tag trees, zero-bit-plane tag trees, coding pass
 * counts, segment lengths, and MQ codewords per Annex B.10), then writes the
 * codestream structure around it.
 *
 * @param file           Open output file stream.
 * @param params         Codestream parameters.
 * @param streams        EBCOT code-block streams.
 * @param stream_count   Number of streams.
 * @return DIC_STATUS_OK or error code.
 */
dic_status j2k_write_ebcot_packet_codestream_stream(
    FILE *file,
    const j2k_basic_params *params,
    const j2k_codeblock_stream *streams,
    size_t stream_count
)
{
    j2k_DEBUG_ENTER();
    j2k_packet_header payload;
    dic_status status;

    if (file == NULL || params == NULL || (streams == NULL && stream_count > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;

    j2k_packet_header_init(&payload);
    status = j2k_packet_build_ebcot_payload(streams, stream_count, &payload);
    if (status == DIC_STATUS_OK)
    {
        status = j2k_write_codestream_with_payload_stream(
            file,
            params,
            payload.data,
            payload.size
        );
    }

    j2k_packet_header_free(&payload);
    return status;
}

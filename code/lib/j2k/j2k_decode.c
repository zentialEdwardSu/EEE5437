/**
 * @file j2k_decode.c
 * @brief Decodes the repository JPEG 2000 Part 1 reversible image subset from J2K and JP2 files.
 *
 * The decoder parses Annex A marker segments, Annex I JP2 boxes, LRCP packet headers,
 * SOP/EPH packet markers, tag-tree inclusion and zero-bit-plane syntax, terminated EBCOT
 * code-block contributions, inverse 5-3 DWT, inverse RCT, and unsigned 8-bit level shifting.
 * It intentionally accepts the constrained profile emitted by j2k_image.c: one precinct
 * per resolution, reversible 5-3 transform, terminated MQ coding passes, no component
 * subsampling, no PPM/PPT packet-header relocation, and 8-bit grey or sRGB images.
 *
 * References: paper/T-REC-T.800-200208 indexed Annex A for codestream syntax, Annex B.10
 * for packet headers and tag trees, Annex D for EBCOT code-block decoding, Annex F for
 * inverse DWT, Annex G for RCT, and Annex I for JP2 boxes.
 */

#include "j2k/j2k_image.h"
#include "j2k/j2k_debug.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec/subband.h"
#include "j2k/j2k_codestream.h"
#include "j2k/j2k_ebcot.h"
#include "j2k/j2k_ict.h"
#include "j2k/j2k_quant.h"
#include "j2k/j2k_rct.h"
#include "j2k/jp2_file.h"
#include "wavelet/dic_dwt53.h"
#include "wavelet/dic_dwt97.h"

enum
{
    j2k_DECODE_CODEBLOCK_SIZE = 64,
    j2k_DECODE_INITIAL_LBLOCK = 3,
    j2k_DECODE_MAX_TAGTREE_VALUE = 64
};

typedef struct j2k_decode_buffer
{
    uint8_t *data;
    size_t size;
} j2k_decode_buffer;

typedef struct j2k_decode_view
{
    const uint8_t *data;
    size_t size;
    size_t offset;
} j2k_decode_view;

typedef struct j2k_decode_tile_part
{
    uint16_t tile_index;
    const uint8_t *payload;
    size_t payload_size;
} j2k_decode_tile_part;

typedef struct j2k_decode_tile_list
{
    j2k_decode_tile_part *parts;
    size_t count;
    size_t capacity;
} j2k_decode_tile_list;

typedef struct j2k_decode_tagtree_level
{
    int width;
    int height;
    size_t offset;
} j2k_decode_tagtree_level;

typedef struct j2k_decode_tagtree_node
{
    uint32_t low;
    int known;
} j2k_decode_tagtree_node;

typedef struct j2k_decode_tagtree
{
    j2k_decode_tagtree_level *levels;
    j2k_decode_tagtree_node *nodes;
    size_t level_count;
    size_t node_count;
} j2k_decode_tagtree;

typedef struct j2k_decode_codeblock
{
    j2k_codeblock_stream stream;
    uint32_t lblock;
} j2k_decode_codeblock;

typedef struct j2k_decode_subband
{
    int component;
    dic_rect_i32 rect;
    j2k_subband_orientation orientation;
    uint32_t nominal_bitplanes;
    int blocks_x;
    int blocks_y;
    j2k_decode_codeblock *blocks;
    j2k_decode_tagtree inclusion_tree;
    j2k_decode_tagtree zero_tree;
} j2k_decode_subband;

typedef struct j2k_decode_bit_reader
{
    const uint8_t *data;
    size_t size;
    size_t byte_offset;
    unsigned int bit_offset;
} j2k_decode_bit_reader;

typedef struct j2k_decode_contribution
{
    j2k_decode_codeblock *block;
    size_t *segment_lengths;
    uint32_t segment_count;
    size_t total_length;
} j2k_decode_contribution;

typedef struct j2k_decode_contribution_list
{
    j2k_decode_contribution *items;
    size_t count;
    size_t capacity;
} j2k_decode_contribution_list;

/**
 * @brief Free the internal buffer of a j2k_decode_buffer.
 *
 * Safe for NULL (early return). After freeing, data is set to NULL and size to 0.
 */
static void j2k_decode_buffer_free(j2k_decode_buffer *buffer)
{
    j2k_DEBUG_ENTER();
    if (buffer == NULL)
        return;
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0u;
}

/**
 * @brief Read an entire file into memory for codestream parsing.
 *
 * Opens the file in binary mode, determines file size via fseek/ftell,
 * allocates a buffer (minimum 1 byte even for empty files), and reads
 * the entire contents. Used for both raw .j2k and .jp2 files.
 *
 * @param path    File path (non-NULL).
 * @param buffer  [out] Buffer with .data and .size populated.
 * @return DIC_STATUS_OK or error (open, read, memory).
 */
static dic_status j2k_decode_read_file(const char *path, j2k_decode_buffer *buffer)
{
    j2k_DEBUG_ENTER();
    FILE *file = NULL;
    long length;

    if (path == NULL || buffer == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    buffer->data = NULL;
    buffer->size = 0u;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0)
        return DIC_STATUS_FILE_OPEN_ERROR;
#else
    file = fopen(path, "rb");
    if (file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;
#endif
    if (fseek(file, 0L, SEEK_END) != 0)
    {
        fclose(file);
        return DIC_STATUS_FILE_READ_ERROR;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0L, SEEK_SET) != 0)
    {
        fclose(file);
        return DIC_STATUS_FILE_READ_ERROR;
    }
    buffer->data = (uint8_t *)malloc((size_t)length == 0u ? 1u : (size_t)length);
    if (buffer->data == NULL)
    {
        fclose(file);
        return DIC_STATUS_MEMORY_ERROR;
    }
    buffer->size = (size_t)length;
    if (buffer->size > 0u && fread(buffer->data, 1u, buffer->size, file) != buffer->size)
    {
        fclose(file);
        j2k_decode_buffer_free(buffer);
        return DIC_STATUS_FILE_READ_ERROR;
    }
    fclose(file);
    return DIC_STATUS_OK;
}

/**
 * @brief Read one unsigned 8-bit byte from the view, advancing the offset.
 * @return 1 on success, 0 if past end of data.
 */
static int j2k_decode_read_u8(j2k_decode_view *view, uint8_t *value)
{
    j2k_DEBUG_ENTER();
    if (view->offset >= view->size)
        return 0;
    *value = view->data[view->offset++];
    return 1;
}

/**
 * @brief Read a 16-bit unsigned big-endian value from the view.
 *
 * Per Annex A, all multi-byte codestream fields are MSB-first.
 * Reads two bytes, combines: hi<<8 | lo.
 * @return 1 on success, 0 if past end of data.
 */
static int j2k_decode_read_u16_be(j2k_decode_view *view, uint16_t *value)
{
    j2k_DEBUG_ENTER();
    uint8_t hi;
    uint8_t lo;

    if (!j2k_decode_read_u8(view, &hi) || !j2k_decode_read_u8(view, &lo))
        return 0;
    *value = (uint16_t)(((uint16_t)hi << 8) | lo);
    return 1;
}

/**
 * @brief Read a 32-bit unsigned big-endian value from the view.
 *
 * Per Annex A, 32-bit fields (Psot, Xsiz, Ysiz, JP2 box lengths)
 * are encoded MSB-first in 4 bytes. Reads b0..b3, combines: b0<<24 | b1<<16 | b2<<8 | b3.
 * @return 1 on success, 0 if past end of data.
 */
static int j2k_decode_read_u32_be(j2k_decode_view *view, uint32_t *value)
{
    j2k_DEBUG_ENTER();
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
    uint8_t b3;

    if (!j2k_decode_read_u8(view, &b0)
        || !j2k_decode_read_u8(view, &b1)
        || !j2k_decode_read_u8(view, &b2)
        || !j2k_decode_read_u8(view, &b3))
    {
        return 0;
    }
    *value = ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | b3;
    return 1;
}

static dic_status j2k_decode_tile_list_push(
    j2k_decode_tile_list *list,
    uint16_t tile_index,
    const uint8_t *payload,
    size_t payload_size
)
{
    j2k_DEBUG_ENTER();
    j2k_decode_tile_part *new_parts;
    size_t new_capacity;

    if (list == NULL || (payload == NULL && payload_size > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;
    if (list->count == list->capacity)
    {
        new_capacity = list->capacity == 0u ? 4u : list->capacity * 2u;
        if (new_capacity < list->capacity)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_parts = (j2k_decode_tile_part *)realloc(
            list->parts,
            new_capacity * sizeof(list->parts[0])
        );
        if (new_parts == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        list->parts = new_parts;
        list->capacity = new_capacity;
    }
    list->parts[list->count].tile_index = tile_index;
    list->parts[list->count].payload = payload;
    list->parts[list->count].payload_size = payload_size;
    ++list->count;
    return DIC_STATUS_OK;
}

static void j2k_decode_tile_list_free(j2k_decode_tile_list *list)
{
    j2k_DEBUG_ENTER();
    if (list == NULL)
        return;
    free(list->parts);
    list->parts = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

/**
 * @brief Parse the SIZ marker segment per Annex A.5.1 Table A.9.
 *
 * Reads Xsiz, Ysiz, XTsiz, YTsiz, Csiz, and per-component SSiz/XRsiz/YRsiz.
 * Validates that SSiz=7 (8-bit unsigned) and XRsiz=YRsiz=1 (no subsampling).
 * Tile dimensions equal to reference grid dimensions are normalized to 0.
 * Minimum length: 41 bytes (38 + 3*1 component). Rejects component counts
 * other than 1 (grey) or 3 (color).
 */
static dic_status j2k_decode_parse_siz(
    j2k_decode_view *view,
    uint16_t length,
    j2k_basic_params *params
)
{
    j2k_DEBUG_ENTER();
    uint16_t rsiz;
    uint32_t ignored;
    uint16_t component;

    if (length < 41u || params == NULL)
        return DIC_J2K_FORMAT_ERROR;
    if (!j2k_decode_read_u16_be(view, &rsiz)
        || !j2k_decode_read_u32_be(view, &params->width)
        || !j2k_decode_read_u32_be(view, &params->height)
        || !j2k_decode_read_u32_be(view, &ignored)
        || !j2k_decode_read_u32_be(view, &ignored)
        || !j2k_decode_read_u32_be(view, &params->tile_width)
        || !j2k_decode_read_u32_be(view, &params->tile_height)
        || !j2k_decode_read_u32_be(view, &ignored)
        || !j2k_decode_read_u32_be(view, &ignored)
        || !j2k_decode_read_u16_be(view, &params->components))
    {
        return DIC_STATUS_FILE_READ_ERROR;
    }
    (void)rsiz;
    if (params->components != 1u && params->components != 3u)
        return DIC_J2K_INVALID_COMPONENTS;
    for (component = 0u; component < params->components; ++component)
    {
        uint8_t ssiz;
        uint8_t xrsiz;
        uint8_t yrsiz;

        if (!j2k_decode_read_u8(view, &ssiz)
            || !j2k_decode_read_u8(view, &xrsiz)
            || !j2k_decode_read_u8(view, &yrsiz))
        {
            return DIC_STATUS_FILE_READ_ERROR;
        }
        if (ssiz != 7u || xrsiz != 1u || yrsiz != 1u)
            return DIC_J2K_FORMAT_ERROR;
    }
    if (params->tile_width == params->width)
        params->tile_width = 0u;
    if (params->tile_height == params->height)
        params->tile_height = 0u;
    return DIC_STATUS_OK;
}

/**
 * @brief Parse the COD marker segment per Annex A.6.1 Tables A.12-A.21.
 *
 * Extracts Scod flags (precincts/SOP/EPH), progression order (must be 0=LRCP),
 * number of layers, MCT flag, decomposition levels, code-block size (must be
 * exponent=4 => 64x64), code-block style (must be 0x04: bypass+causal+regular
 * per D.5.2), wavelet transform (0=9-7, 1=5-3). When precincts are enabled,
 * reads PPx/PPy for each resolution level r=0..N_L (each byte: PPy<<4|PPx),
 * requiring PPx=PPy=15 (max precinct) per the constrained decoder profile.
 */
static dic_status j2k_decode_parse_cod(
    j2k_decode_view *view,
    uint16_t length,
    j2k_basic_params *params
)
{
    j2k_DEBUG_ENTER();
    uint8_t scod;
    uint8_t progression;
    uint8_t codeblock_width;
    uint8_t codeblock_height;
    uint8_t codeblock_style;
    uint8_t transform;
    uint8_t resolution;
    uint16_t expected_length;

    if (length < 12u || params == NULL)
        return DIC_J2K_FORMAT_ERROR;
    if (!j2k_decode_read_u8(view, &scod)
        || !j2k_decode_read_u8(view, &progression)
        || !j2k_decode_read_u16_be(view, &params->layers)
        || !j2k_decode_read_u8(view, &params->multiple_component_transform)
        || !j2k_decode_read_u8(view, &params->decomposition_levels)
        || !j2k_decode_read_u8(view, &codeblock_width)
        || !j2k_decode_read_u8(view, &codeblock_height)
        || !j2k_decode_read_u8(view, &codeblock_style)
        || !j2k_decode_read_u8(view, &transform))
    {
        return DIC_STATUS_FILE_READ_ERROR;
    }
    params->use_precincts = (uint8_t)((scod & 0x01u) != 0u);
    params->use_sop = (uint8_t)((scod & 0x02u) != 0u);
    params->use_eph = (uint8_t)((scod & 0x04u) != 0u);
    params->reversible = transform == 1u ? 1u : 0u;
    if (progression != 0u || params->layers == 0u)
        return DIC_J2K_FORMAT_ERROR;
    if (codeblock_width != 4u || codeblock_height != 4u || codeblock_style != 0x04u)
        return DIC_J2K_FORMAT_ERROR;
    if (params->decomposition_levels > j2k_MAX_DECOMPOSITION_LEVELS)
        return DIC_J2K_INVALID_LEVELS;

    expected_length = (uint16_t)(12u + (params->use_precincts ? params->decomposition_levels + 1u : 0u));
    if (length != expected_length)
        return DIC_J2K_FORMAT_ERROR;
    for (resolution = 0u; params->use_precincts && resolution <= params->decomposition_levels; ++resolution)
    {
        uint8_t precinct;

        if (!j2k_decode_read_u8(view, &precinct))
            return DIC_STATUS_FILE_READ_ERROR;
        params->precinct_width_exponents[resolution] = (uint8_t)(precinct & 0x0fu);
        params->precinct_height_exponents[resolution] = (uint8_t)(precinct >> 4);
        if (precinct != 0xffu)
            return DIC_J2K_UNSUPPORTED_PRECINCT_SIZE;
    }
    return DIC_STATUS_OK;
}

static unsigned int j2k_decode_irreversible_qcd_range_bits(unsigned int step_index)
{
    if (step_index == 0u)
        return 8u;
    return ((step_index - 1u) % 3u) == 2u ? 10u : 9u;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.6.4, QCD supplies quantization step sizes needed by irreversible decoding. */
static dic_status j2k_decode_parse_qcd(
    j2k_decode_view *view,
    uint16_t length,
    j2k_basic_params *params
)
{
    uint8_t sqcd;
    uint16_t remaining;

    if (length < 3u || params == NULL)
        return DIC_J2K_FORMAT_ERROR;
    if (!j2k_decode_read_u8(view, &sqcd))
        return DIC_STATUS_FILE_READ_ERROR;

    remaining = (uint16_t)(length - 3u);
    params->quant_step_count = 0u;
    params->quant_guard_bits = (uint8_t)(sqcd >> 5);
    if ((sqcd & 0x1fu) == 0u)
    {
        if (remaining > j2k_MAX_QUANT_STEPS || remaining > view->size - view->offset)
            return DIC_J2K_FORMAT_ERROR;
        view->offset += remaining;
        params->quant_step_count = remaining;
        return DIC_STATUS_OK;
    }
    if ((sqcd & 0x1fu) == 2u)
    {
        uint16_t index;

        if ((remaining % 2u) != 0u || remaining / 2u > j2k_MAX_QUANT_STEPS)
            return DIC_J2K_FORMAT_ERROR;
        for (index = 0u; index < remaining / 2u; ++index)
        {
            uint16_t spqcd;

            if (!j2k_decode_read_u16_be(view, &spqcd))
                return DIC_STATUS_FILE_READ_ERROR;
            if (j2k_quant_decode_irreversible_spqcd(
                    spqcd,
                    j2k_decode_irreversible_qcd_range_bits(index),
                    params->quant_step_sizes + index
                ) != DIC_STATUS_OK)
            {
                return DIC_J2K_FORMAT_ERROR;
            }
        }
        params->quant_step_count = (uint16_t)(remaining / 2u);
        return DIC_STATUS_OK;
    }
    return DIC_J2K_FORMAT_ERROR;
}

static dic_status j2k_decode_parse_codestream(
    const uint8_t *data,
    size_t size,
    j2k_basic_params *params,
    j2k_decode_tile_list *tiles
)
{
    j2k_DEBUG_ENTER();
    j2k_decode_view view;
    uint16_t marker;

    if (data == NULL || params == NULL || tiles == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    memset(params, 0, sizeof(*params));
    view.data = data;
    view.size = size;
    view.offset = 0u;
    if (!j2k_decode_read_u16_be(&view, &marker) || marker != j2k_MARKER_SOC)
        return DIC_J2K_FORMAT_ERROR;

    while (view.offset < view.size)
    {
        uint16_t length;
        dic_status status = DIC_STATUS_OK;

        if (!j2k_decode_read_u16_be(&view, &marker))
            return DIC_J2K_FORMAT_ERROR;
        if (marker == j2k_MARKER_EOC)
        {
            if (!params->reversible
                && params->quant_step_count != (uint16_t)(1u + 3u * (unsigned int)params->decomposition_levels))
            {
                return DIC_J2K_FORMAT_ERROR;
            }
            return DIC_STATUS_OK;
        }
        if (marker == j2k_MARKER_SOT)
        {
            uint16_t tile_index;
            uint32_t psot;
            uint8_t tpsot;
            uint8_t tnsot;
            size_t payload_size;

            if (!j2k_decode_read_u16_be(&view, &length) || length != 10u)
                return DIC_J2K_FORMAT_ERROR;
            if (!j2k_decode_read_u16_be(&view, &tile_index)
                || !j2k_decode_read_u32_be(&view, &psot)
                || !j2k_decode_read_u8(&view, &tpsot)
                || !j2k_decode_read_u8(&view, &tnsot))
            {
                return DIC_STATUS_FILE_READ_ERROR;
            }
            (void)tpsot;
            (void)tnsot;
            if (!j2k_decode_read_u16_be(&view, &marker) || marker != j2k_MARKER_SOD)
                return DIC_J2K_FORMAT_ERROR;
            if (psot < 14u)
                return DIC_J2K_FORMAT_ERROR;
            payload_size = (size_t)psot - 14u;
            if (payload_size > view.size - view.offset)
                return DIC_J2K_FORMAT_ERROR;
            status = j2k_decode_tile_list_push(
                tiles,
                tile_index,
                view.data + view.offset,
                payload_size
            );
            if (status != DIC_STATUS_OK)
                return status;
            view.offset += payload_size;
            continue;
        }

        if (!j2k_decode_read_u16_be(&view, &length) || length < 2u)
            return DIC_STATUS_FILE_READ_ERROR;
        if (length - 2u > view.size - view.offset)
            return DIC_J2K_FORMAT_ERROR;
        if (marker == j2k_MARKER_SIZ)
            status = j2k_decode_parse_siz(&view, length, params);
        else if (marker == j2k_MARKER_COD)
            status = j2k_decode_parse_cod(&view, length, params);
        else if (marker == j2k_MARKER_QCD)
            status = j2k_decode_parse_qcd(&view, length, params);
        else
            view.offset += (size_t)length - 2u;
        if (status != DIC_STATUS_OK)
            return status;
    }
    return DIC_J2K_FORMAT_ERROR;
}

static void j2k_decode_tagtree_init(j2k_decode_tagtree *tree)
{
    j2k_DEBUG_ENTER();
    tree->levels = NULL;
    tree->nodes = NULL;
    tree->level_count = 0u;
    tree->node_count = 0u;
}

static void j2k_decode_tagtree_free(j2k_decode_tagtree *tree)
{
    j2k_DEBUG_ENTER();
    if (tree == NULL)
        return;
    free(tree->levels);
    free(tree->nodes);
    j2k_decode_tagtree_init(tree);
}

/**
 * @brief Allocate and initialize a tag tree for a code-block grid.
 *
 * Per Annex B.10.4-B.10.5, tag trees are hierarchical structures where each
 * level halves the grid dimensions. The total node count is sum of w_i*h_i
 * across all levels. Nodes store a lower bound (low) and a known flag.
 *
 * @param tree    [out] Tag tree to allocate.
 * @param width   Code-block grid width (blocks_x).
 * @param height  Code-block grid height (blocks_y).
 * @return DIC_STATUS_OK, DIC_STATUS_MEMORY_ERROR, or DIC_STATUS_INVALID_ARGUMENT.
 */
static dic_status j2k_decode_tagtree_alloc(j2k_decode_tagtree *tree, int width, int height)
{
    j2k_DEBUG_ENTER();
    int current_width;
    int current_height;
    size_t levels = 0u;
    size_t nodes = 0u;
    size_t level;

    if (tree == NULL || width <= 0 || height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    j2k_decode_tagtree_free(tree);
    current_width = width;
    current_height = height;
    while (current_width > 0 && current_height > 0)
    {
        if ((size_t)current_width > ((size_t)-1 - nodes) / (size_t)current_height)
            return DIC_STATUS_INVALID_ARGUMENT;
        nodes += (size_t)current_width * (size_t)current_height;
        ++levels;
        if (current_width == 1 && current_height == 1)
            break;
        current_width = (current_width + 1) / 2;
        current_height = (current_height + 1) / 2;
    }
    tree->levels = (j2k_decode_tagtree_level *)calloc(levels, sizeof(tree->levels[0]));
    tree->nodes = (j2k_decode_tagtree_node *)calloc(nodes, sizeof(tree->nodes[0]));
    if (tree->levels == NULL || tree->nodes == NULL)
    {
        j2k_decode_tagtree_free(tree);
        return DIC_STATUS_MEMORY_ERROR;
    }
    tree->level_count = levels;
    tree->node_count = nodes;
    current_width = width;
    current_height = height;
    nodes = 0u;
    for (level = 0u; level < levels; ++level)
    {
        tree->levels[level].width = current_width;
        tree->levels[level].height = current_height;
        tree->levels[level].offset = nodes;
        nodes += (size_t)current_width * (size_t)current_height;
        current_width = (current_width + 1) / 2;
        current_height = (current_height + 1) / 2;
    }
    return DIC_STATUS_OK;
}

static size_t j2k_decode_tagtree_index(
    const j2k_decode_tagtree *tree,
    size_t level,
    int x,
    int y
)
{
    j2k_DEBUG_ENTER();
    return tree->levels[level].offset
        + (size_t)y * (size_t)tree->levels[level].width
        + (size_t)x;
}

/**
 * @brief Initialize a bit reader over a packet data buffer.
 *
 * The reader maintains a byte offset and a sub-byte bit offset (0-7),
 * enabling bit-level reading of packet headers per Annex B.10.
 */
static void j2k_decode_bit_reader_init(
    j2k_decode_bit_reader *reader,
    const uint8_t *data,
    size_t size
)
{
    j2k_DEBUG_ENTER();
    reader->data = data;
    reader->size = size;
    reader->byte_offset = 0u;
    reader->bit_offset = 0u;
}

/**
 * @brief Return the number of usable bits in the current byte.
 *
 * Per Annex A.3 and Annex B.10.1, the single bit-stuffing rule applies:
 * if the previous byte was 0xFF, the current byte starts with only 7 bits
 * (the MSB is a stuffing zero). Otherwise, all 8 bits are valid data bits.
 * This implements the in-bit-stream marker avoidance mechanism.
 */
static unsigned int j2k_decode_reader_bits_in_current(const j2k_decode_bit_reader *reader)
{
    j2k_DEBUG_ENTER();
    /** If previous byte was 0xFF, MSB of current byte is a stuffing 0 => 7 data bits. */
    return reader->byte_offset > 0u && reader->data[reader->byte_offset - 1u] == 0xffu ? 7u : 8u;
}

/**
 * @brief Read a single bit from the packet bit-stream.
 *
 * Extracts the current bit (MSB-first within each byte), advances bit_offset,
 * and when the byte is exhausted, advances to the next byte. The bits_in_current
 * check implements the 0xFF bit-stuffing rule per Annex B.10.1.
 */
static dic_status j2k_decode_read_packet_bit(
    j2k_decode_bit_reader *reader,
    uint32_t *bit
)
{
    j2k_DEBUG_ENTER();
    unsigned int bits_in_current;
    unsigned int shift;

    if (reader == NULL || bit == NULL || reader->byte_offset >= reader->size)
        return DIC_J2K_FORMAT_ERROR;
    bits_in_current = j2k_decode_reader_bits_in_current(reader);
    shift = bits_in_current - 1u - reader->bit_offset;
    *bit = (reader->data[reader->byte_offset] >> shift) & 1u;
    ++reader->bit_offset;
    if (reader->bit_offset == bits_in_current)
    {
        ++reader->byte_offset;
        reader->bit_offset = 0u;
    }
    return DIC_STATUS_OK;
}

/**
 * @brief Read multiple bits from the packet bit-stream (MSB first).
 *
 * Reads bit_count bits one at a time (max 31), building the result value
 * by left-shifting and OR-ing each successive bit. Each bit read follows
 * the 0xFF stuffing rule via j2k_decode_read_packet_bit().
 */
static dic_status j2k_decode_read_packet_bits(
    j2k_decode_bit_reader *reader,
    uint32_t bit_count,
    uint32_t *value
)
{
    j2k_DEBUG_ENTER();
    uint32_t index;
    uint32_t result = 0u;

    if (value == NULL || bit_count > 31u)
        return DIC_STATUS_INVALID_ARGUMENT;
    for (index = 0u; index < bit_count; ++index)
    {
        uint32_t bit;
        dic_status status = j2k_decode_read_packet_bit(reader, &bit);

        if (status != DIC_STATUS_OK)
            return status;
        result = (result << 1) | bit;
    }
    *value = result;
    return DIC_STATUS_OK;
}

static size_t j2k_decode_reader_aligned_offset(const j2k_decode_bit_reader *reader)
{
    j2k_DEBUG_ENTER();
    return reader->byte_offset + (reader->bit_offset == 0u ? 0u : 1u);
}

/**
 * @brief Decode a tag tree leaf node per Annex B.10.4-B.10.5.
 *
 * Tag trees encode the minimum threshold at which each code-block first becomes
 * included (inclusion tag tree) or the number of zero bit-planes (zero tag tree).
 * The tree is traversed from leaf to root. At each node, if the node's known
 * lower bound is below the current threshold, zero bits are read until a 1-bit
 * terminates (known) or the threshold is exceeded (unknown). If unknown at any
 * level, the function returns early with known=0 and value=threshold+1.
 *
 * @param tree      Tag tree structure.
 * @param x/y       Leaf position (code-block coordinates).
 * @param threshold Current layer index or candidate value.
 * @param reader    Bit reader for packet header data.
 * @param known     [out] 1 if value is known, 0 if still unknown.
 * @param value     [out] Decoded value (meaningful only if known==1).
 * @return DIC_STATUS_OK or format error.
 */
static dic_status j2k_decode_tagtree_leaf(
    j2k_decode_tagtree *tree,
    int x,
    int y,
    uint32_t threshold,
    j2k_decode_bit_reader *reader,
    int *known,
    uint32_t *value
)
{
    j2k_DEBUG_ENTER();
    size_t level;
    uint32_t lower_bound = 0u;

    if (tree == NULL || reader == NULL || known == NULL || value == NULL || tree->nodes == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    *known = 0;
    *value = threshold + 1u;
    for (level = tree->level_count; level > 0u; --level)
    {
        size_t actual_level = level - 1u;
        int node_x = x >> actual_level;
        int node_y = y >> actual_level;
        j2k_decode_tagtree_node *node;

        if (node_x >= tree->levels[actual_level].width)
            node_x = tree->levels[actual_level].width - 1;
        if (node_y >= tree->levels[actual_level].height)
            node_y = tree->levels[actual_level].height - 1;
        node = tree->nodes + j2k_decode_tagtree_index(tree, actual_level, node_x, node_y);
        if (node->low < lower_bound)
            node->low = lower_bound;
        if (!node->known)
        {
            while (node->low <= threshold)
            {
                uint32_t bit;
                dic_status status = j2k_decode_read_packet_bit(reader, &bit);

                if (status != DIC_STATUS_OK)
                    return status;
                if (bit == 0u)
                    ++node->low;
                else
                {
                    node->known = 1;
                    break;
                }
            }
        }
        lower_bound = node->low;
        if (!node->known)
            return DIC_STATUS_OK;
    }
    *known = 1;
    *value = lower_bound;
    return DIC_STATUS_OK;
}

/**
 * @brief Decode a tag tree value by iterating thresholds 0..max.
 *
 * Calls j2k_decode_tagtree_leaf() with increasing thresholds until the
 * tree signals "known" or max is reached. Used for zero-bit-plane tag trees
 * where the leaf value is the number of zero bit-planes (no upper bound
 * known a priori).
 */
static dic_status j2k_decode_tagtree_value(
    j2k_decode_tagtree *tree,
    int x,
    int y,
    j2k_decode_bit_reader *reader,
    uint32_t *value
)
{
    j2k_DEBUG_ENTER();
    uint32_t threshold;

    for (threshold = 0u; threshold <= j2k_DECODE_MAX_TAGTREE_VALUE; ++threshold)
    {
        int known;
        dic_status status = j2k_decode_tagtree_leaf(tree, x, y, threshold, reader, &known, value);

        if (status != DIC_STATUS_OK)
            return status;
        if (known)
            return DIC_STATUS_OK;
    }
    return DIC_J2K_FORMAT_ERROR;
}

/**
 * @brief Decode number of coding passes from a packet header per Annex B.10.6.
 *
 * Variable-length code: 0=1 pass, 10=2 passes, 1100xx=3..6 (add 3 to xx),
 * 1101xxxxx=6..36 (add 6), 1110xxxxxxx=37..163 (add 37). Maximum: 164 passes
 * per code-block per layer. This maps to the Cleanup/SigProp/MagRef pass
 * sequence of Annex D.5.
 */
static dic_status j2k_decode_coding_passes(
    j2k_decode_bit_reader *reader,
    uint32_t *passes
)
{
    j2k_DEBUG_ENTER();
    uint32_t bit;
    uint32_t value;
    dic_status status;

    status = j2k_decode_read_packet_bit(reader, &bit);
    if (status != DIC_STATUS_OK)
        return status;
    if (bit == 0u)
    {
        *passes = 1u;
        return DIC_STATUS_OK;
    }
    status = j2k_decode_read_packet_bit(reader, &bit);
    if (status != DIC_STATUS_OK)
        return status;
    if (bit == 0u)
    {
        *passes = 2u;
        return DIC_STATUS_OK;
    }
    status = j2k_decode_read_packet_bits(reader, 2u, &value);
    if (status != DIC_STATUS_OK)
        return status;
    if (value < 3u)
    {
        *passes = value + 3u;
        return DIC_STATUS_OK;
    }
    status = j2k_decode_read_packet_bits(reader, 5u, &value);
    if (status != DIC_STATUS_OK)
        return status;
    if (value < 31u)
    {
        *passes = value + 6u;
        return DIC_STATUS_OK;
    }
    status = j2k_decode_read_packet_bits(reader, 7u, &value);
    if (status != DIC_STATUS_OK)
        return status;
    *passes = value + 37u;
    return *passes <= 164u ? DIC_STATUS_OK : DIC_J2K_FORMAT_ERROR;
}

static void j2k_decode_contribution_list_free(j2k_decode_contribution_list *list)
{
    j2k_DEBUG_ENTER();
    size_t index;

    if (list == NULL)
        return;
    for (index = 0u; index < list->count; ++index)
        free(list->items[index].segment_lengths);
    free(list->items);
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static dic_status j2k_decode_contribution_list_push(
    j2k_decode_contribution_list *list,
    j2k_decode_codeblock *block,
    size_t *segment_lengths,
    uint32_t segment_count,
    size_t total_length
)
{
    j2k_DEBUG_ENTER();
    j2k_decode_contribution *new_items;
    size_t new_capacity;

    if (list == NULL || block == NULL || segment_lengths == NULL || segment_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (list->count == list->capacity)
    {
        new_capacity = list->capacity == 0u ? 8u : list->capacity * 2u;
        if (new_capacity < list->capacity)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_items = (j2k_decode_contribution *)realloc(
            list->items,
            new_capacity * sizeof(list->items[0])
        );
        if (new_items == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        list->items = new_items;
        list->capacity = new_capacity;
    }
    list->items[list->count].block = block;
    list->items[list->count].segment_lengths = segment_lengths;
    list->items[list->count].segment_count = segment_count;
    list->items[list->count].total_length = total_length;
    ++list->count;
    return DIC_STATUS_OK;
}

static dic_status j2k_decode_stream_append(
    j2k_codeblock_stream *stream,
    const uint8_t *codeword,
    size_t codeword_size,
    const size_t *segment_lengths,
    uint32_t segment_count
)
{
    j2k_DEBUG_ENTER();
    uint8_t *new_data;
    size_t *new_lengths;
    uint32_t old_passes = stream->coding_passes;

    if (stream == NULL || (codeword == NULL && codeword_size > 0u) || segment_lengths == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (codeword_size > (size_t)-1 - stream->mq.byte_count)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (segment_count > UINT32_MAX - stream->coding_passes)
        return DIC_STATUS_INVALID_ARGUMENT;
    new_data = (uint8_t *)realloc(stream->mq.data, stream->mq.byte_count + codeword_size);
    if (new_data == NULL && stream->mq.byte_count + codeword_size > 0u)
        return DIC_STATUS_MEMORY_ERROR;
    stream->mq.data = new_data;
    if (codeword_size > 0u)
        memcpy(stream->mq.data + stream->mq.byte_count, codeword, codeword_size);
    new_lengths = (size_t *)realloc(
        stream->pass_lengths,
        ((size_t)stream->coding_passes + segment_count) * sizeof(stream->pass_lengths[0])
    );
    if (new_lengths == NULL)
        return DIC_STATUS_MEMORY_ERROR;
    stream->pass_lengths = new_lengths;
    memcpy(stream->pass_lengths + old_passes, segment_lengths, (size_t)segment_count * sizeof(segment_lengths[0]));
    stream->mq.byte_count += codeword_size;
    stream->coding_passes += segment_count;
    stream->mq.bit_count = (size_t)-1;
    return DIC_STATUS_OK;
}

/**
 * @brief Decode a single LRCP packet per Annex B.10.
 *
 * Decodes the packet header (SOP marker if present, nonempty bit, inclusion
 * tag tree per B.10.4, zero-bit-plane tag tree per B.10.5, coding pass count
 * per B.10.6, coded segment lengths per B.10.7, EPH marker if present), then
 * extracts MQ codeword bytes from the packet body and appends them to the
 * corresponding code-block streams via j2k_decode_stream_append(). The stream
 * accumulates MQ data across layers per Annex C.2-C.4.
 *
 * The lblock field tracks the minimum coded length indicator size for subsequent
 * layers, initialized to 3 per Annex B.10.7.
 */
static dic_status j2k_decode_packet(
    const uint8_t *payload,
    size_t payload_size,
    size_t *offset,
    j2k_decode_subband *subbands,
    size_t subband_count,
    uint16_t layer_index,
    const j2k_basic_params *params,
    uint16_t packet_sequence
)
{
    j2k_DEBUG_ENTER();
    j2k_decode_bit_reader reader;
    j2k_decode_contribution_list contributions = {0};
    size_t header_size;
    size_t index;
    dic_status status = DIC_STATUS_OK;
    uint32_t nonempty;

    if (payload == NULL || offset == NULL || subbands == NULL || params == NULL || *offset > payload_size)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (params->use_sop)
    {
        if (payload_size - *offset < 6u
            || payload[*offset] != 0xffu
            || payload[*offset + 1u] != 0x91u
            || payload[*offset + 2u] != 0x00u
            || payload[*offset + 3u] != 0x04u)
        {
            return DIC_J2K_FORMAT_ERROR;
        }
        (void)packet_sequence;
        *offset += 6u;
    }

    j2k_decode_bit_reader_init(&reader, payload + *offset, payload_size - *offset);
    status = j2k_decode_read_packet_bit(&reader, &nonempty);
    if (status != DIC_STATUS_OK)
        return status;
    if (nonempty)
    {
        size_t subband_index;

        for (subband_index = 0u; subband_index < subband_count && status == DIC_STATUS_OK; ++subband_index)
        {
            j2k_decode_subband *subband = subbands + subband_index;
            int by;

            for (by = 0; by < subband->blocks_y && status == DIC_STATUS_OK; ++by)
            {
                int bx;

                for (bx = 0; bx < subband->blocks_x && status == DIC_STATUS_OK; ++bx)
                {
                    size_t block_index = (size_t)by * (size_t)subband->blocks_x + (size_t)bx;
                    j2k_decode_codeblock *block = subband->blocks + block_index;
                    int first_inclusion = block->stream.coding_passes == 0u;
                    uint32_t included = 0u;

                    if (first_inclusion)
                    {
                        int known;
                        uint32_t value;

                        status = j2k_decode_tagtree_leaf(
                            &subband->inclusion_tree,
                            bx,
                            by,
                            layer_index,
                            &reader,
                            &known,
                            &value
                        );
                        if (status != DIC_STATUS_OK)
                            break;
                        included = (uint32_t)(known && value <= layer_index);
                    }
                    else
                    {
                        status = j2k_decode_read_packet_bit(&reader, &included);
                        if (status != DIC_STATUS_OK)
                            break;
                    }
                    if (included)
                    {
                        uint32_t pass_count;
                        uint32_t lblock_increment = 0u;
                        size_t *segment_lengths = NULL;
                        size_t total_length = 0u;
                        uint32_t pass;

                        if (first_inclusion)
                        {
                            uint32_t zero_bitplanes;

                            status = j2k_decode_tagtree_value(
                                &subband->zero_tree,
                                bx,
                                by,
                                &reader,
                                &zero_bitplanes
                            );
                            if (status != DIC_STATUS_OK)
                                break;
                            block->stream.zero_bitplanes = zero_bitplanes;
                            block->stream.magnitude_bitplanes = subband->nominal_bitplanes > zero_bitplanes
                                ? subband->nominal_bitplanes - zero_bitplanes
                                : 0u;
                        }
                        status = j2k_decode_coding_passes(&reader, &pass_count);
                        if (status != DIC_STATUS_OK)
                            break;
                        do
                        {
                            uint32_t bit;

                            status = j2k_decode_read_packet_bit(&reader, &bit);
                            if (status != DIC_STATUS_OK)
                                break;
                            if (bit == 0u)
                                break;
                            ++lblock_increment;
                        } while (lblock_increment < 32u);
                        if (status != DIC_STATUS_OK)
                            break;
                        block->lblock += lblock_increment;
                        if (block->lblock > 31u)
                        {
                            status = DIC_J2K_FORMAT_ERROR;
                            break;
                        }
                        segment_lengths = (size_t *)calloc(pass_count, sizeof(segment_lengths[0]));
                        if (segment_lengths == NULL)
                        {
                            status = DIC_STATUS_MEMORY_ERROR;
                            break;
                        }
                        for (pass = 0u; pass < pass_count; ++pass)
                        {
                            uint32_t length_value;

                            status = j2k_decode_read_packet_bits(&reader, block->lblock, &length_value);
                            if (status != DIC_STATUS_OK)
                                break;
                            segment_lengths[pass] = length_value;
                            total_length += length_value;
                        }
                        if (status == DIC_STATUS_OK)
                        {
                            status = j2k_decode_contribution_list_push(
                                &contributions,
                                block,
                                segment_lengths,
                                pass_count,
                                total_length
                            );
                            if (status == DIC_STATUS_OK)
                                segment_lengths = NULL;
                        }
                        free(segment_lengths);
                    }
                }
            }
        }
    }

    if (status == DIC_STATUS_OK)
    {
        header_size = j2k_decode_reader_aligned_offset(&reader);
        if (header_size > payload_size - *offset)
            status = DIC_J2K_FORMAT_ERROR;
        else
            *offset += header_size;
    }
    if (status == DIC_STATUS_OK && params->use_eph)
    {
        if (payload_size - *offset < 2u || payload[*offset] != 0xffu || payload[*offset + 1u] != 0x92u)
            status = DIC_J2K_FORMAT_ERROR;
        else
            *offset += 2u;
    }
    for (index = 0u; status == DIC_STATUS_OK && index < contributions.count; ++index)
    {
        j2k_decode_contribution *contribution = contributions.items + index;

        if (contribution->total_length > payload_size - *offset)
        {
            status = DIC_J2K_FORMAT_ERROR;
            break;
        }
        status = j2k_decode_stream_append(
            &contribution->block->stream,
            payload + *offset,
            contribution->total_length,
            contribution->segment_lengths,
            contribution->segment_count
        );
        if (status != DIC_STATUS_OK)
            break;
        *offset += contribution->total_length;
    }

    j2k_decode_contribution_list_free(&contributions);
    return status;
}

static j2k_subband_orientation j2k_decode_orientation(codec_subband_orientation orientation)
{
    j2k_DEBUG_ENTER();
    if (orientation == DIC_SUBBAND_HL)
        return j2k_SUBBAND_HL;
    if (orientation == DIC_SUBBAND_HH)
        return j2k_SUBBAND_HH;
    return j2k_SUBBAND_LL_LH;
}

static void j2k_decode_subband_free(j2k_decode_subband *subband)
{
    j2k_DEBUG_ENTER();
    size_t index;
    size_t count;

    if (subband == NULL)
        return;
    count = (size_t)subband->blocks_x * (size_t)subband->blocks_y;
    for (index = 0u; index < count; ++index)
        j2k_codeblock_stream_free(&subband->blocks[index].stream);
    free(subband->blocks);
    j2k_decode_tagtree_free(&subband->inclusion_tree);
    j2k_decode_tagtree_free(&subband->zero_tree);
}

static void j2k_decode_subbands_free(j2k_decode_subband *subbands, size_t count)
{
    j2k_DEBUG_ENTER();
    size_t index;

    if (subbands == NULL)
        return;
    for (index = 0u; index < count; ++index)
        j2k_decode_subband_free(subbands + index);
    free(subbands);
}

static dic_status j2k_decode_init_subband_blocks(j2k_decode_subband *subband)
{
    j2k_DEBUG_ENTER();
    int by;
    dic_status status;

    subband->blocks_x = (subband->rect.width + j2k_DECODE_CODEBLOCK_SIZE - 1) / j2k_DECODE_CODEBLOCK_SIZE;
    subband->blocks_y = (subband->rect.height + j2k_DECODE_CODEBLOCK_SIZE - 1) / j2k_DECODE_CODEBLOCK_SIZE;
    subband->blocks = (j2k_decode_codeblock *)calloc(
        (size_t)subband->blocks_x * (size_t)subband->blocks_y,
        sizeof(subband->blocks[0])
    );
    if (subband->blocks == NULL)
        return DIC_STATUS_MEMORY_ERROR;
    status = j2k_decode_tagtree_alloc(&subband->inclusion_tree, subband->blocks_x, subband->blocks_y);
    if (status == DIC_STATUS_OK)
        status = j2k_decode_tagtree_alloc(&subband->zero_tree, subband->blocks_x, subband->blocks_y);
    if (status != DIC_STATUS_OK)
        return status;
    for (by = 0; by < subband->blocks_y; ++by)
    {
        int bx;

        for (bx = 0; bx < subband->blocks_x; ++bx)
        {
            size_t block_index = (size_t)by * (size_t)subband->blocks_x + (size_t)bx;
            int remaining_width = subband->rect.width - bx * j2k_DECODE_CODEBLOCK_SIZE;
            int remaining_height = subband->rect.height - by * j2k_DECODE_CODEBLOCK_SIZE;
            j2k_decode_codeblock *block = subband->blocks + block_index;

            j2k_codeblock_stream_init(&block->stream);
            block->lblock = j2k_DECODE_INITIAL_LBLOCK;
            block->stream.width = (uint32_t)(remaining_width < j2k_DECODE_CODEBLOCK_SIZE
                ? remaining_width
                : j2k_DECODE_CODEBLOCK_SIZE);
            block->stream.height = (uint32_t)(remaining_height < j2k_DECODE_CODEBLOCK_SIZE
                ? remaining_height
                : j2k_DECODE_CODEBLOCK_SIZE);
            block->stream.subband_orientation = (uint8_t)subband->orientation;
        }
    }
    return DIC_STATUS_OK;
}

/**
 * @brief Build the sub-band array for all components and resolution levels.
 *
 * Creates per_component = 1 + 3*NL sub-band descriptors per component
 * (1 LL + 3 per resolution level for HL/LH/HH). Each sub-band has its
 * nominal bit-plane count (9 for LL, 10 for HL/LH, 11 for HH, plus chroma
 * extra bits), computed rectangle via codec_subband_rect()/codec_subband_lowest_ll_rect(),
 * and code-block grid with inclusion/zero tag trees initialized.
 */
static dic_status j2k_decode_make_subbands(
    int width,
    int height,
    int components,
    int levels,
    j2k_decode_subband **subbands_out,
    size_t *count_out
)
{
    j2k_DEBUG_ENTER();
    size_t per_component = 1u + (size_t)levels * 3u;
    size_t count = (size_t)components * per_component;
    j2k_decode_subband *subbands;
    size_t out = 0u;
    int component;
    dic_status status = DIC_STATUS_OK;

    if (subbands_out == NULL || count_out == NULL || width <= 0 || height <= 0 || components <= 0 || levels < 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    subbands = (j2k_decode_subband *)calloc(count, sizeof(subbands[0]));
    if (subbands == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    for (component = 0; component < components && status == DIC_STATUS_OK; ++component)
    {
        uint32_t extra_bits = components == 3 && component > 0 ? 1u : 0u;
        int resolution;

        j2k_decode_tagtree_init(&subbands[out].inclusion_tree);
        j2k_decode_tagtree_init(&subbands[out].zero_tree);
        subbands[out].component = component;
        subbands[out].orientation = j2k_SUBBAND_LL_LH;
        subbands[out].nominal_bitplanes = 9u + extra_bits;
        if (levels == 0)
        {
            subbands[out].rect.x = 0;
            subbands[out].rect.y = 0;
            subbands[out].rect.width = width;
            subbands[out].rect.height = height;
        }
        else
        {
            status = codec_subband_lowest_ll_rect(width, height, levels, &subbands[out].rect);
        }
        if (status == DIC_STATUS_OK)
            status = j2k_decode_init_subband_blocks(subbands + out);
        ++out;

        for (resolution = 1; resolution <= levels && status == DIC_STATUS_OK; ++resolution)
        {
            static const codec_subband_orientation orientations[] = {
                DIC_SUBBAND_HL,
                DIC_SUBBAND_LH,
                DIC_SUBBAND_HH
            };
            size_t orientation_index;
            int level = levels - resolution + 1;

            for (orientation_index = 0u; orientation_index < 3u && status == DIC_STATUS_OK; ++orientation_index)
            {
                j2k_decode_tagtree_init(&subbands[out].inclusion_tree);
                j2k_decode_tagtree_init(&subbands[out].zero_tree);
                subbands[out].component = component;
                subbands[out].orientation = j2k_decode_orientation(orientations[orientation_index]);
                subbands[out].nominal_bitplanes = (orientations[orientation_index] == DIC_SUBBAND_HH ? 11u : 10u)
                    + extra_bits;
                status = codec_subband_rect(width, height, levels, level, orientations[orientation_index], &subbands[out].rect);
                if (status == DIC_STATUS_OK)
                    status = j2k_decode_init_subband_blocks(subbands + out);
                ++out;
            }
        }
    }
    if (status != DIC_STATUS_OK)
    {
        j2k_decode_subbands_free(subbands, count);
        return status;
    }
    *subbands_out = subbands;
    *count_out = count;
    return DIC_STATUS_OK;
}

static uint32_t j2k_decode_lossy_nominal_bitplanes(
    double step_size,
    unsigned int step_index,
    uint8_t guard_bits
)
{
    uint16_t spqcd;
    uint32_t exponent;

    if (j2k_quant_encode_irreversible_spqcd(
            step_size,
            j2k_decode_irreversible_qcd_range_bits(step_index),
            &spqcd
        ) != DIC_STATUS_OK)
    {
        return 1u;
    }
    exponent = (uint32_t)(spqcd >> 11);
    return guard_bits > 0u ? exponent + (uint32_t)guard_bits - 1u : exponent;
}

static dic_status j2k_decode_set_lossy_nominal_bitplanes(
    j2k_decode_subband *subbands,
    size_t subband_count,
    const j2k_basic_params *params
)
{
    size_t per_component;
    uint16_t expected_steps;
    uint16_t component;

    if (subbands == NULL || params == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    per_component = 1u + (size_t)params->decomposition_levels * 3u;
    expected_steps = (uint16_t)per_component;
    if (params->quant_step_count != expected_steps)
        return DIC_J2K_FORMAT_ERROR;
    if (subband_count != per_component * (size_t)params->components)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (component = 0u; component < params->components; ++component)
    {
        size_t step;

        for (step = 0u; step < per_component; ++step)
        {
            subbands[(size_t)component * per_component + step].nominal_bitplanes =
                j2k_decode_lossy_nominal_bitplanes(
                    params->quant_step_sizes[step],
                    (unsigned int)step,
                    params->quant_guard_bits
                );
        }
    }
    return DIC_STATUS_OK;
}

static j2k_decode_subband *j2k_decode_packet_subbands(
    j2k_decode_subband *subbands,
    size_t subband_count,
    int component,
    int levels,
    int resolution,
    size_t *count
)
{
    j2k_DEBUG_ENTER();
    size_t per_component = 1u + (size_t)levels * 3u;
    size_t start = (size_t)component * per_component + (resolution == 0 ? 0u : 1u + (size_t)(resolution - 1) * 3u);

    if (start >= subband_count)
        return NULL;
    *count = resolution == 0 ? 1u : 3u;
    return subbands + start;
}

/**
 * @brief Decode all EBCOT code-blocks and place coefficients into planar buffer.
 *
 * For each sub-band, iterates over its code-block grid. Each block is decoded
 * via j2k_ebcot_decode_codeblock_rect() (Annex D: MQ decoder with Cleanup/SigProp/
 * MagRef passes per Annex C.2-C.4), then the decoded int32 coefficients are
 * memcpy'd into the correct position in the planar coefficient array. The
 * destination position accounts for the sub-band rectangle (rect.x, rect.y) and
 * code-block offset (bx, by) within the code-block grid per Annex B.7.
 */
static dic_status j2k_decode_subbands_to_planes(
    j2k_decode_subband *subbands,
    size_t subband_count,
    int32_t *planes,
    int width,
    int height,
    int components
)
{
    j2k_DEBUG_ENTER();
    size_t subband_index;
    size_t plane_samples = (size_t)width * (size_t)height;

    for (subband_index = 0u; subband_index < subband_count; ++subband_index)
    {
        j2k_decode_subband *subband = subbands + subband_index;
        size_t block_count = (size_t)subband->blocks_x * (size_t)subband->blocks_y;
        size_t block_index;

        for (block_index = 0u; block_index < block_count; ++block_index)
        {
            j2k_decode_codeblock *block = subband->blocks + block_index;
            int bx = (int)(block_index % (size_t)subband->blocks_x);
            int by = (int)(block_index / (size_t)subband->blocks_x);
            int32_t *decoded;
            uint32_t y;
            dic_status status;

            decoded = (int32_t *)calloc(
                (size_t)block->stream.width * (size_t)block->stream.height,
                sizeof(decoded[0])
            );
            if (decoded == NULL)
                return DIC_STATUS_MEMORY_ERROR;
            status = j2k_ebcot_decode_codeblock_rect(
                &block->stream,
                block->stream.width,
                block->stream.height,
                subband->orientation,
                decoded
            );
            if (status != DIC_STATUS_OK)
            {
                free(decoded);
                return status;
            }
            for (y = 0u; y < block->stream.height; ++y)
            {
                int dst_x = subband->rect.x + bx * j2k_DECODE_CODEBLOCK_SIZE;
                int dst_y = subband->rect.y + by * j2k_DECODE_CODEBLOCK_SIZE + (int)y;
                int32_t *plane = planes + (size_t)subband->component * plane_samples;

                if (subband->component < 0 || subband->component >= components)
                {
                    free(decoded);
                    return DIC_STATUS_INVALID_ARGUMENT;
                }
                memcpy(
                    plane + (size_t)dst_y * (size_t)width + (size_t)dst_x,
                    decoded + (size_t)y * (size_t)block->stream.width,
                    (size_t)block->stream.width * sizeof(decoded[0])
                );
            }
            free(decoded);
        }
    }
    return DIC_STATUS_OK;
}

/**
 * @brief Reverse DC level shift and clamp to unsigned 8-bit [0, 255].
 *
 * Per Annex G.1/G.2: I(x,y) = I'(x,y) + 128. The clamp handles out-of-range
 * values that may arise from lossy coding or numerical imprecision in DWT/IDWT.
 */
static uint8_t j2k_decode_unshift_u8(int32_t value)
{
    j2k_DEBUG_ENTER();
    int32_t shifted = value + 128;

    if (shifted < 0)
        return 0u;
    if (shifted > 255)
        return 255u;
    return (uint8_t)shifted;
}

/**
 * @brief Reverse DC level shift for double-precision values with rounding.
 *
 * Uses lround() for proper rounding of double to integer before the +128
 * shift and [0,255] clamp. Used with irreversible (9-7 DWT + ICT) decoding.
 */
static uint8_t j2k_decode_unshift_double_u8(double value)
{
    long rounded = lround(value + 128.0);

    if (rounded < 0L)
        return 0u;
    if (rounded > 255L)
        return 255u;
    return (uint8_t)rounded;
}

/**
 * @brief Inverse quantize coefficients in a rectangular region.
 *
 * Per Annex E.1.1 Equation E-2: y = q * delta_b + sign(q)*delta_b/2.
 * Applies j2k_dequantize_coefficient() to each sample in the sub-band
 * rectangle, reconstructing double-precision DWT coefficients from
 * quantized integer values.
 */
static dic_status j2k_decode_dequantize_rect(
    const int32_t *source,
    double *target,
    int plane_width,
    const dic_rect_i32 *rect,
    double step_size
)
{
    int y;

    if (source == NULL || target == NULL || rect == NULL || rect->width <= 0 || rect->height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (y = 0; y < rect->height; ++y)
    {
        int x;

        for (x = 0; x < rect->width; ++x)
        {
            /** Compute 1D offset into the plane accounting for rect origin. */
            size_t offset = (size_t)(rect->y + y) * (size_t)plane_width + (size_t)(rect->x + x);
            dic_status status = j2k_dequantize_coefficient(source[offset], step_size, target + offset);

            if (status != DIC_STATUS_OK)
                return status;
        }
    }
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex E, inverse quantization reconstructs irreversible DWT coefficients before IDWT. */

/**
 * @brief Inverse quantization: reconstruct double-precision coefficients from int32.
 *
 * Per Annex E.1.1 Equation E-2: y(u,v) = q(u,v) * delta_b + sign(q)*delta_b/2
 * (midpoint reconstruction with dead-zone). Validates step_count matches
 * expected: 1 + 3*NL. Iterates per sub-band using codec_subband_rect() for
 * geometry, applying j2k_dequantize_coefficient().
 */
static dic_status j2k_decode_dequantize_planes(
    const int32_t *source,
    double *target,
    int width,
    int height,
    int components,
    int levels,
    const double *steps,
    uint16_t step_count
)
{
    size_t plane_samples = (size_t)width * (size_t)height;
    int component;

    if (step_count != (uint16_t)(1u + 3u * (unsigned int)levels))
        return DIC_J2K_FORMAT_ERROR;

    for (component = 0; component < components; ++component)
    {
        const int32_t *source_plane = source + (size_t)component * plane_samples;
        double *target_plane = target + (size_t)component * plane_samples;
        int resolution;
        dic_rect_i32 rect;
        dic_status status;

        status = levels == 0 ? DIC_STATUS_OK : codec_subband_lowest_ll_rect(width, height, levels, &rect);
        if (levels == 0)
        {
            rect.x = 0;
            rect.y = 0;
            rect.width = width;
            rect.height = height;
        }
        if (status == DIC_STATUS_OK)
            status = j2k_decode_dequantize_rect(source_plane, target_plane, width, &rect, steps[0]);
        for (resolution = 1; status == DIC_STATUS_OK && resolution <= levels; ++resolution)
        {
            static const codec_subband_orientation orientations[] = {
                DIC_SUBBAND_HL,
                DIC_SUBBAND_LH,
                DIC_SUBBAND_HH
            };
            int index;
            int level = levels - resolution + 1;

            for (index = 0; status == DIC_STATUS_OK && index < 3; ++index)
            {
                status = codec_subband_rect(width, height, levels, level, orientations[index], &rect);
                if (status == DIC_STATUS_OK)
                {
                    status = j2k_decode_dequantize_rect(
                        source_plane,
                        target_plane,
                        width,
                        &rect,
                        steps[1 + (resolution - 1) * 3 + index]
                    );
                }
            }
        }
        if (status != DIC_STATUS_OK)
            return status;
    }
    return DIC_STATUS_OK;
}

/**
 * @brief Decode a complete tile payload: packets -> sub-bands -> IDWT -> output image.
 *
 * Full decode pipeline for one tile:
 * 1. Allocate int32 planes and sub-band descriptors (including tag trees).
 * 2. For each layer/resolution/component: decode packet via j2k_decode_packet(),
 *    accumulating MQ codewords into code-block streams across layers.
 * 3. EBCOT decode all code-blocks, placing coefficients into the plane array
 *    via j2k_decode_subbands_to_planes().
 * 4. If reversible: apply inverse 5-3 DWT (Annex F.3) to each component.
 *    If irreversible: dequantize (Annex E.1.1), apply inverse 9-7 DWT (Annex F.4).
 * 5. If MCT active: inverse RCT (Annex G.2) or inverse ICT (Annex G.1).
 * 6. Level unshift: add 128 and clamp to [0, 255] unsigned 8-bit.
 *
 * @param payload / payload_size  Tile-part bit-stream data.
 * @param params                  Parsed codestream parameters.
 * @param max_layers              Maximum layers to decode (0 = all).
 * @param tile_width / height     Tile dimensions.
 * @param tile                    [out] Decoded tile image.
 * @return DIC_STATUS_OK or error.
 */
static dic_status j2k_decode_tile_payload(
    const uint8_t *payload,
    size_t payload_size,
    const j2k_basic_params *params,
    uint16_t max_layers,
    int tile_width,
    int tile_height,
    dic_image_u8 *tile
)
{
    j2k_DEBUG_ENTER();
    j2k_decode_subband *subbands = NULL;
    size_t subband_count = 0u;
    int32_t *planes = NULL;
    double *double_planes = NULL;
    size_t plane_samples;
    size_t offset = 0u;
    uint16_t packet_sequence = 0u;
    uint16_t layer;
    uint16_t layers_to_decode;
    dic_status status;
    int component;

    if (payload == NULL || params == NULL || tile == NULL || tile_width <= 0 || tile_height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (max_layers > params->layers)
        return DIC_STATUS_INVALID_ARGUMENT;
    layers_to_decode = max_layers == 0u ? params->layers : max_layers;
    if (layers_to_decode == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    plane_samples = (size_t)tile_width * (size_t)tile_height;
    planes = (int32_t *)calloc(plane_samples * (size_t)params->components, sizeof(planes[0]));
    if (planes == NULL)
        return DIC_STATUS_MEMORY_ERROR;
    status = j2k_decode_make_subbands(
        tile_width,
        tile_height,
        (int)params->components,
        (int)params->decomposition_levels,
        &subbands,
        &subband_count
    );
    if (status == DIC_STATUS_OK && !params->reversible)
        status = j2k_decode_set_lossy_nominal_bitplanes(subbands, subband_count, params);
    for (layer = 0u; status == DIC_STATUS_OK && layer < layers_to_decode; ++layer)
    {
        int resolution;

        for (resolution = 0; status == DIC_STATUS_OK && resolution <= (int)params->decomposition_levels; ++resolution)
        {
            for (component = 0; status == DIC_STATUS_OK && component < (int)params->components; ++component)
            {
                size_t packet_subband_count = 0u;
                j2k_decode_subband *packet_subbands = j2k_decode_packet_subbands(
                    subbands,
                    subband_count,
                    component,
                    (int)params->decomposition_levels,
                    resolution,
                    &packet_subband_count
                );

                if (packet_subbands == NULL)
                {
                    status = DIC_STATUS_INVALID_ARGUMENT;
                    break;
                }
                status = j2k_decode_packet(
                    payload,
                    payload_size,
                    &offset,
                    packet_subbands,
                    packet_subband_count,
                    layer,
                    params,
                    packet_sequence
                );
                ++packet_sequence;
            }
        }
    }
    if (status == DIC_STATUS_OK && layers_to_decode == params->layers && offset != payload_size)
        status = DIC_J2K_FORMAT_ERROR;
    if (status == DIC_STATUS_OK)
        status = j2k_decode_subbands_to_planes(
            subbands,
            subband_count,
            planes,
            tile_width,
            tile_height,
            (int)params->components
        );
    if (status == DIC_STATUS_OK && params->reversible)
    {
        for (component = 0; status == DIC_STATUS_OK
             && params->decomposition_levels > 0u
             && component < (int)params->components; ++component)
        {
            status = dic_dwt53_inverse_plane(
                planes + (size_t)component * plane_samples,
                tile_width,
                tile_height,
                (int)params->decomposition_levels
            );
        }
    }
    else if (status == DIC_STATUS_OK)
    {
        double_planes = (double *)calloc(plane_samples * (size_t)params->components, sizeof(double_planes[0]));
        if (double_planes == NULL)
            status = DIC_STATUS_MEMORY_ERROR;
        if (status == DIC_STATUS_OK)
        {
            status = j2k_decode_dequantize_planes(
                planes,
                double_planes,
                tile_width,
                tile_height,
                (int)params->components,
                (int)params->decomposition_levels,
                params->quant_step_sizes,
                params->quant_step_count
            );
        }
        for (component = 0; status == DIC_STATUS_OK
             && params->decomposition_levels > 0u
             && component < (int)params->components; ++component)
        {
            status = dic_dwt97_inverse_plane(
                double_planes + (size_t)component * plane_samples,
                tile_width,
                tile_height,
                (int)params->decomposition_levels
            );
        }
    }
    if (status == DIC_STATUS_OK)
        status = dic_image_u8_alloc(tile, tile_width, tile_height, (int)params->components);
    if (status == DIC_STATUS_OK && params->reversible && params->components == 3u && params->multiple_component_transform)
    {
        int32_t *interleaved = (int32_t *)malloc(plane_samples * 3u * sizeof(interleaved[0]));
        size_t pixel;

        if (interleaved == NULL)
            status = DIC_STATUS_MEMORY_ERROR;
        else
        {
            for (pixel = 0u; pixel < plane_samples; ++pixel)
            {
                interleaved[pixel * 3u + 0u] = planes[pixel];
                interleaved[pixel * 3u + 1u] = planes[plane_samples + pixel];
                interleaved[pixel * 3u + 2u] = planes[plane_samples * 2u + pixel];
            }
            status = j2k_rct_inverse(interleaved, plane_samples);
            if (status == DIC_STATUS_OK)
            {
                for (pixel = 0u; pixel < plane_samples; ++pixel)
                {
                    tile->data[pixel * 3u + 0u] = j2k_decode_unshift_u8(interleaved[pixel * 3u + 0u]);
                    tile->data[pixel * 3u + 1u] = j2k_decode_unshift_u8(interleaved[pixel * 3u + 1u]);
                    tile->data[pixel * 3u + 2u] = j2k_decode_unshift_u8(interleaved[pixel * 3u + 2u]);
                }
            }
            free(interleaved);
        }
    }
    else if (status == DIC_STATUS_OK && !params->reversible && params->components == 3u && params->multiple_component_transform)
    {
        double *interleaved = (double *)malloc(plane_samples * 3u * sizeof(interleaved[0]));
        size_t pixel;

        if (interleaved == NULL)
            status = DIC_STATUS_MEMORY_ERROR;
        else
        {
            for (pixel = 0u; pixel < plane_samples; ++pixel)
            {
                interleaved[pixel * 3u + 0u] = double_planes[pixel];
                interleaved[pixel * 3u + 1u] = double_planes[plane_samples + pixel];
                interleaved[pixel * 3u + 2u] = double_planes[plane_samples * 2u + pixel];
            }
            status = j2k_ict_inverse(interleaved, plane_samples);
            if (status == DIC_STATUS_OK)
            {
                for (pixel = 0u; pixel < plane_samples; ++pixel)
                {
                    tile->data[pixel * 3u + 0u] = j2k_decode_unshift_double_u8(interleaved[pixel * 3u + 0u]);
                    tile->data[pixel * 3u + 1u] = j2k_decode_unshift_double_u8(interleaved[pixel * 3u + 1u]);
                    tile->data[pixel * 3u + 2u] = j2k_decode_unshift_double_u8(interleaved[pixel * 3u + 2u]);
                }
            }
            free(interleaved);
        }
    }
    else if (status == DIC_STATUS_OK && !params->reversible)
    {
        size_t sample;

        for (sample = 0u; sample < plane_samples * (size_t)params->components; ++sample)
            tile->data[sample] = j2k_decode_unshift_double_u8(double_planes[sample]);
    }
    else if (status == DIC_STATUS_OK)
    {
        size_t sample;

        for (sample = 0u; sample < plane_samples * (size_t)params->components; ++sample)
            tile->data[sample] = j2k_decode_unshift_u8(planes[sample]);
    }

    free(double_planes);
    free(planes);
    j2k_decode_subbands_free(subbands, subband_count);
    return status;
}

static const j2k_decode_tile_part *j2k_decode_find_tile(
    const j2k_decode_tile_list *tiles,
    uint16_t tile_index
)
{
    j2k_DEBUG_ENTER();
    size_t index;

    for (index = 0u; index < tiles->count; ++index)
    {
        if (tiles->parts[index].tile_index == tile_index)
            return tiles->parts + index;
    }
    return NULL;
}

/**
 * @brief Decode a complete codestream in memory to an unsigned 8-bit image.
 *
 * 1. Parse codestream markers (SOC, SIZ, COD, QCD, SOT, SOD) via
 *    j2k_decode_parse_codestream() to extract parameters and tile-part offsets.
 * 2. Compute tile grid dimensions and allocate output image buffer.
 * 3. For each tile in raster order, locate the tile-part payload and decode it
 *    via j2k_decode_tile_payload(), then copy decoded samples to the output image.
 * 4. Tiles are assembled in their correct spatial positions per Annex B.5.
 *
 * @param data        Complete codestream bytes (non-NULL).
 * @param size        Number of codestream bytes.
 * @param max_layers  Limit decoded layers (0 = all layers).
 * @param image       [out] Allocated decoded image.
 * @return DIC_STATUS_OK or error.
 */
static dic_status j2k_decode_image_from_codestream(
    const uint8_t *data,
    size_t size,
    uint16_t max_layers,
    dic_image_u8 *image
)
{
    j2k_DEBUG_ENTER();
    j2k_basic_params params;
    j2k_decode_tile_list tiles = {0};
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t tiles_x;
    uint32_t tiles_y;
    uint32_t tile_index = 0u;
    dic_status status;

    if (data == NULL || image == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    status = j2k_decode_parse_codestream(data, size, &params, &tiles);
    if (status != DIC_STATUS_OK)
    {
        j2k_decode_tile_list_free(&tiles);
        return status;
    }
    if (params.width == 0u || params.height == 0u)
        status = DIC_J2K_INVALID_DIMENSIONS;
    if (status == DIC_STATUS_OK && params.components != 1u && params.components != 3u)
        status = DIC_J2K_INVALID_COMPONENTS;
    if (status == DIC_STATUS_OK && max_layers > params.layers)
        status = DIC_STATUS_INVALID_ARGUMENT;
    tile_width = params.tile_width == 0u ? params.width : params.tile_width;
    tile_height = params.tile_height == 0u ? params.height : params.tile_height;
    if (status == DIC_STATUS_OK && (tile_width == 0u || tile_height == 0u))
        status = DIC_J2K_INVALID_DIMENSIONS;
    if (status == DIC_STATUS_OK)
        status = dic_image_u8_alloc(image, (int)params.width, (int)params.height, (int)params.components);
    tiles_x = tile_width == 0u ? 0u : (params.width + tile_width - 1u) / tile_width;
    tiles_y = tile_height == 0u ? 0u : (params.height + tile_height - 1u) / tile_height;
    while (status == DIC_STATUS_OK && tile_index < tiles_x * tiles_y)
    {
        const j2k_decode_tile_part *part = j2k_decode_find_tile(&tiles, (uint16_t)tile_index);
        uint32_t tx = tile_index % tiles_x;
        uint32_t ty = tile_index / tiles_x;
        int current_width = (int)((tx + 1u == tiles_x) ? params.width - tx * tile_width : tile_width);
        int current_height = (int)((ty + 1u == tiles_y) ? params.height - ty * tile_height : tile_height);
        dic_image_u8 tile;
        int y;

        dic_image_u8_init(&tile);
        if (part == NULL)
        {
            status = DIC_J2K_FORMAT_ERROR;
            break;
        }
        status = j2k_decode_tile_payload(
            part->payload,
            part->payload_size,
            &params,
            max_layers,
            current_width,
            current_height,
            &tile
        );
        for (y = 0; status == DIC_STATUS_OK && y < current_height; ++y)
        {
            memcpy(
                image->data
                    + (((size_t)ty * (size_t)tile_height + (size_t)y) * (size_t)params.width
                        + (size_t)tx * (size_t)tile_width) * (size_t)params.components,
                tile.data + (size_t)y * (size_t)current_width * (size_t)params.components,
                (size_t)current_width * (size_t)params.components
            );
        }
        dic_image_u8_free(&tile);
        ++tile_index;
    }
    j2k_decode_tile_list_free(&tiles);
    return status;
}

/**
 * @brief Read and decode a raw JPEG 2000 codestream (.j2k) file.
 *
 * Reads the file into memory and decodes via j2k_decode_image_from_codestream().
 * All quality layers are decoded (max_layers=0). Delegates to the _layers variant.
 *
 * @param path   Input file path (.j2k).
 * @param image  [out] Decoded unsigned 8-bit image.
 * @return DIC_STATUS_OK or error.
 */
dic_status j2k_read_image_codestream(const char *path, dic_image_u8 *image)
{
    return j2k_read_image_codestream_layers(path, 0u, image);
}

/**
 * @brief Read and decode a raw codestream with layer limit for progressive display.
 *
 * Reads the file into a j2k_decode_buffer, then decodes up to max_layers
 * quality layers. A value of 0 decodes all layers. Useful for testing
 * progressive quality refinement.
 */
dic_status j2k_read_image_codestream_layers(const char *path, uint16_t max_layers, dic_image_u8 *image)
{
    j2k_DEBUG_ENTER();
    j2k_decode_buffer buffer;
    dic_status status;

    if (path == NULL || image == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    status = j2k_decode_read_file(path, &buffer);
    if (status == DIC_STATUS_OK)
        status = j2k_decode_image_from_codestream(buffer.data, buffer.size, max_layers, image);
    j2k_decode_buffer_free(&buffer);
    return status;
}

/**
 * @brief Read and decode a JP2 file (.jp2) per Annex I.
 *
 * Parses JP2 boxes to locate the Contiguous Codestream box (jp2c) per Annex I.5.2.1,
 * then decodes the embedded codestream. All quality layers are decoded.
 *
 * @param path   Input file path (.jp2).
 * @param image  [out] Decoded unsigned 8-bit image.
 * @return DIC_STATUS_OK or error.
 */
dic_status j2k_read_image_jp2(const char *path, dic_image_u8 *image)
{
    return j2k_read_image_jp2_layers(path, 0u, image);
}

/**
 * @brief Read and decode a JP2 file with layer limit.
 *
 * Scans JP2 boxes using a view reader. When the jp2c box type (jp2_BOX_JP2C) is
 * found, extracts the codestream data and decodes up to max_layers via
 * j2k_decode_image_from_codestream(). Box length handling follows Annex I.3-I.5:
 * length=0 means box extends to EOF, length=1 means 8-byte extended length.
 */
dic_status j2k_read_image_jp2_layers(const char *path, uint16_t max_layers, dic_image_u8 *image)
{
    j2k_DEBUG_ENTER();
    j2k_decode_buffer buffer;
    j2k_decode_view view;
    dic_status status;

    if (path == NULL || image == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    status = j2k_decode_read_file(path, &buffer);
    if (status != DIC_STATUS_OK)
        return status;
    view.data = buffer.data;
    view.size = buffer.size;
    view.offset = 0u;
    status = DIC_J2K_FORMAT_ERROR;
    while (view.offset + 8u <= view.size)
    {
        uint32_t length;
        uint32_t type;
        size_t box_start = view.offset;
        size_t payload_start;
        size_t box_size;

        if (!j2k_decode_read_u32_be(&view, &length) || !j2k_decode_read_u32_be(&view, &type))
            break;
        payload_start = view.offset;
        if (length == 1u || (length != 0u && length < 8u))
            break;
        box_size = length == 0u ? view.size - box_start : (size_t)length;
        if (box_size < 8u || box_size > view.size - box_start)
            break;
        if (type == jp2_BOX_JP2C)
        {
            status = j2k_decode_image_from_codestream(
                buffer.data + payload_start,
                box_size - 8u,
                max_layers,
                image
            );
            break;
        }
        view.offset = box_start + box_size;
    }
    j2k_decode_buffer_free(&buffer);
    return status;
}

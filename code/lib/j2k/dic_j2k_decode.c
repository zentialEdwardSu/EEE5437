/**
 * @file dic_j2k_decode.c
 * @brief Decodes the repository JPEG 2000 Part 1 reversible image subset from J2K and JP2 files.
 *
 * The decoder parses Annex A marker segments, Annex I JP2 boxes, LRCP packet headers,
 * SOP/EPH packet markers, tag-tree inclusion and zero-bit-plane syntax, terminated EBCOT
 * code-block contributions, inverse 5-3 DWT, inverse RCT, and unsigned 8-bit level shifting.
 * It intentionally accepts the constrained profile emitted by dic_j2k_image.c: one precinct
 * per resolution, reversible 5-3 transform, terminated MQ coding passes, no component
 * subsampling, no PPM/PPT packet-header relocation, and 8-bit grey or sRGB images.
 *
 * References: paper/T-REC-T.800-200208 indexed Annex A for codestream syntax, Annex B.10
 * for packet headers and tag trees, Annex D for EBCOT code-block decoding, Annex F for
 * inverse DWT, Annex G for RCT, and Annex I for JP2 boxes.
 */

#include "j2k/dic_j2k_image.h"
#include "j2k/dic_j2k_debug.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec/dic_subband.h"
#include "j2k/dic_j2k_codestream.h"
#include "j2k/dic_j2k_ebcot.h"
#include "j2k/dic_j2k_rct.h"
#include "j2k/dic_jp2_file.h"
#include "wavelet/dic_dwt53.h"

enum
{
    DIC_J2K_DECODE_CODEBLOCK_SIZE = 64,
    DIC_J2K_DECODE_INITIAL_LBLOCK = 3,
    DIC_J2K_DECODE_MAX_TAGTREE_VALUE = 64
};

typedef struct dic_j2k_decode_buffer
{
    uint8_t *data;
    size_t size;
} dic_j2k_decode_buffer;

typedef struct dic_j2k_decode_view
{
    const uint8_t *data;
    size_t size;
    size_t offset;
} dic_j2k_decode_view;

typedef struct dic_j2k_decode_tile_part
{
    uint16_t tile_index;
    const uint8_t *payload;
    size_t payload_size;
} dic_j2k_decode_tile_part;

typedef struct dic_j2k_decode_tile_list
{
    dic_j2k_decode_tile_part *parts;
    size_t count;
    size_t capacity;
} dic_j2k_decode_tile_list;

typedef struct dic_j2k_decode_tagtree_level
{
    int width;
    int height;
    size_t offset;
} dic_j2k_decode_tagtree_level;

typedef struct dic_j2k_decode_tagtree_node
{
    uint32_t low;
    int known;
} dic_j2k_decode_tagtree_node;

typedef struct dic_j2k_decode_tagtree
{
    dic_j2k_decode_tagtree_level *levels;
    dic_j2k_decode_tagtree_node *nodes;
    size_t level_count;
    size_t node_count;
} dic_j2k_decode_tagtree;

typedef struct dic_j2k_decode_codeblock
{
    dic_j2k_codeblock_stream stream;
    uint32_t lblock;
} dic_j2k_decode_codeblock;

typedef struct dic_j2k_decode_subband
{
    int component;
    dic_rect_i32 rect;
    dic_j2k_subband_orientation orientation;
    uint32_t nominal_bitplanes;
    int blocks_x;
    int blocks_y;
    dic_j2k_decode_codeblock *blocks;
    dic_j2k_decode_tagtree inclusion_tree;
    dic_j2k_decode_tagtree zero_tree;
} dic_j2k_decode_subband;

typedef struct dic_j2k_decode_bit_reader
{
    const uint8_t *data;
    size_t size;
    size_t byte_offset;
    unsigned int bit_offset;
} dic_j2k_decode_bit_reader;

typedef struct dic_j2k_decode_contribution
{
    dic_j2k_decode_codeblock *block;
    size_t *segment_lengths;
    uint32_t segment_count;
    size_t total_length;
} dic_j2k_decode_contribution;

typedef struct dic_j2k_decode_contribution_list
{
    dic_j2k_decode_contribution *items;
    size_t count;
    size_t capacity;
} dic_j2k_decode_contribution_list;

static void dic_j2k_decode_buffer_free(dic_j2k_decode_buffer *buffer)
{
    DIC_J2K_DEBUG_ENTER();
    if (buffer == NULL)
        return;
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0u;
}

static dic_status dic_j2k_decode_read_file(const char *path, dic_j2k_decode_buffer *buffer)
{
    DIC_J2K_DEBUG_ENTER();
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
        dic_j2k_decode_buffer_free(buffer);
        return DIC_STATUS_FILE_READ_ERROR;
    }
    fclose(file);
    return DIC_STATUS_OK;
}

static int dic_j2k_decode_read_u8(dic_j2k_decode_view *view, uint8_t *value)
{
    DIC_J2K_DEBUG_ENTER();
    if (view->offset >= view->size)
        return 0;
    *value = view->data[view->offset++];
    return 1;
}

static int dic_j2k_decode_read_u16_be(dic_j2k_decode_view *view, uint16_t *value)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t hi;
    uint8_t lo;

    if (!dic_j2k_decode_read_u8(view, &hi) || !dic_j2k_decode_read_u8(view, &lo))
        return 0;
    *value = (uint16_t)(((uint16_t)hi << 8) | lo);
    return 1;
}

static int dic_j2k_decode_read_u32_be(dic_j2k_decode_view *view, uint32_t *value)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
    uint8_t b3;

    if (!dic_j2k_decode_read_u8(view, &b0)
        || !dic_j2k_decode_read_u8(view, &b1)
        || !dic_j2k_decode_read_u8(view, &b2)
        || !dic_j2k_decode_read_u8(view, &b3))
    {
        return 0;
    }
    *value = ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | b3;
    return 1;
}

static dic_status dic_j2k_decode_tile_list_push(
    dic_j2k_decode_tile_list *list,
    uint16_t tile_index,
    const uint8_t *payload,
    size_t payload_size
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_decode_tile_part *new_parts;
    size_t new_capacity;

    if (list == NULL || (payload == NULL && payload_size > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;
    if (list->count == list->capacity)
    {
        new_capacity = list->capacity == 0u ? 4u : list->capacity * 2u;
        if (new_capacity < list->capacity)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_parts = (dic_j2k_decode_tile_part *)realloc(
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

static void dic_j2k_decode_tile_list_free(dic_j2k_decode_tile_list *list)
{
    DIC_J2K_DEBUG_ENTER();
    if (list == NULL)
        return;
    free(list->parts);
    list->parts = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static dic_status dic_j2k_decode_parse_siz(
    dic_j2k_decode_view *view,
    uint16_t length,
    dic_j2k_basic_params *params
)
{
    DIC_J2K_DEBUG_ENTER();
    uint16_t rsiz;
    uint32_t ignored;
    uint16_t component;

    if (length < 41u || params == NULL)
        return DIC_J2K_FORMAT_ERROR;
    if (!dic_j2k_decode_read_u16_be(view, &rsiz)
        || !dic_j2k_decode_read_u32_be(view, &params->width)
        || !dic_j2k_decode_read_u32_be(view, &params->height)
        || !dic_j2k_decode_read_u32_be(view, &ignored)
        || !dic_j2k_decode_read_u32_be(view, &ignored)
        || !dic_j2k_decode_read_u32_be(view, &params->tile_width)
        || !dic_j2k_decode_read_u32_be(view, &params->tile_height)
        || !dic_j2k_decode_read_u32_be(view, &ignored)
        || !dic_j2k_decode_read_u32_be(view, &ignored)
        || !dic_j2k_decode_read_u16_be(view, &params->components))
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

        if (!dic_j2k_decode_read_u8(view, &ssiz)
            || !dic_j2k_decode_read_u8(view, &xrsiz)
            || !dic_j2k_decode_read_u8(view, &yrsiz))
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

static dic_status dic_j2k_decode_parse_cod(
    dic_j2k_decode_view *view,
    uint16_t length,
    dic_j2k_basic_params *params
)
{
    DIC_J2K_DEBUG_ENTER();
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
    if (!dic_j2k_decode_read_u8(view, &scod)
        || !dic_j2k_decode_read_u8(view, &progression)
        || !dic_j2k_decode_read_u16_be(view, &params->layers)
        || !dic_j2k_decode_read_u8(view, &params->multiple_component_transform)
        || !dic_j2k_decode_read_u8(view, &params->decomposition_levels)
        || !dic_j2k_decode_read_u8(view, &codeblock_width)
        || !dic_j2k_decode_read_u8(view, &codeblock_height)
        || !dic_j2k_decode_read_u8(view, &codeblock_style)
        || !dic_j2k_decode_read_u8(view, &transform))
    {
        return DIC_STATUS_FILE_READ_ERROR;
    }
    params->use_precincts = (uint8_t)((scod & 0x01u) != 0u);
    params->use_sop = (uint8_t)((scod & 0x02u) != 0u);
    params->use_eph = (uint8_t)((scod & 0x04u) != 0u);
    params->reversible = transform == 1u ? 1u : 0u;
    if (progression != 0u || params->layers == 0u || !params->reversible)
        return DIC_J2K_FORMAT_ERROR;
    if (codeblock_width != 4u || codeblock_height != 4u || codeblock_style != 0x04u)
        return DIC_J2K_FORMAT_ERROR;
    if (params->decomposition_levels > DIC_J2K_MAX_DECOMPOSITION_LEVELS)
        return DIC_J2K_INVALID_LEVELS;

    expected_length = (uint16_t)(12u + (params->use_precincts ? params->decomposition_levels + 1u : 0u));
    if (length != expected_length)
        return DIC_J2K_FORMAT_ERROR;
    for (resolution = 0u; params->use_precincts && resolution <= params->decomposition_levels; ++resolution)
    {
        uint8_t precinct;

        if (!dic_j2k_decode_read_u8(view, &precinct))
            return DIC_STATUS_FILE_READ_ERROR;
        params->precinct_width_exponents[resolution] = (uint8_t)(precinct & 0x0fu);
        params->precinct_height_exponents[resolution] = (uint8_t)(precinct >> 4);
        if (precinct != 0xffu)
            return DIC_J2K_UNSUPPORTED_PRECINCT_SIZE;
    }
    return DIC_STATUS_OK;
}

static dic_status dic_j2k_decode_parse_codestream(
    const uint8_t *data,
    size_t size,
    dic_j2k_basic_params *params,
    dic_j2k_decode_tile_list *tiles
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_decode_view view;
    uint16_t marker;

    if (data == NULL || params == NULL || tiles == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    memset(params, 0, sizeof(*params));
    view.data = data;
    view.size = size;
    view.offset = 0u;
    if (!dic_j2k_decode_read_u16_be(&view, &marker) || marker != DIC_J2K_MARKER_SOC)
        return DIC_J2K_FORMAT_ERROR;

    while (view.offset < view.size)
    {
        uint16_t length;
        dic_status status = DIC_STATUS_OK;

        if (!dic_j2k_decode_read_u16_be(&view, &marker))
            return DIC_J2K_FORMAT_ERROR;
        if (marker == DIC_J2K_MARKER_EOC)
            return DIC_STATUS_OK;
        if (marker == DIC_J2K_MARKER_SOT)
        {
            uint16_t tile_index;
            uint32_t psot;
            uint8_t tpsot;
            uint8_t tnsot;
            size_t payload_size;

            if (!dic_j2k_decode_read_u16_be(&view, &length) || length != 10u)
                return DIC_J2K_FORMAT_ERROR;
            if (!dic_j2k_decode_read_u16_be(&view, &tile_index)
                || !dic_j2k_decode_read_u32_be(&view, &psot)
                || !dic_j2k_decode_read_u8(&view, &tpsot)
                || !dic_j2k_decode_read_u8(&view, &tnsot))
            {
                return DIC_STATUS_FILE_READ_ERROR;
            }
            (void)tpsot;
            (void)tnsot;
            if (!dic_j2k_decode_read_u16_be(&view, &marker) || marker != DIC_J2K_MARKER_SOD)
                return DIC_J2K_FORMAT_ERROR;
            if (psot < 14u)
                return DIC_J2K_FORMAT_ERROR;
            payload_size = (size_t)psot - 14u;
            if (payload_size > view.size - view.offset)
                return DIC_J2K_FORMAT_ERROR;
            status = dic_j2k_decode_tile_list_push(
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

        if (!dic_j2k_decode_read_u16_be(&view, &length) || length < 2u)
            return DIC_STATUS_FILE_READ_ERROR;
        if (length - 2u > view.size - view.offset)
            return DIC_J2K_FORMAT_ERROR;
        if (marker == DIC_J2K_MARKER_SIZ)
            status = dic_j2k_decode_parse_siz(&view, length, params);
        else if (marker == DIC_J2K_MARKER_COD)
            status = dic_j2k_decode_parse_cod(&view, length, params);
        else
            view.offset += (size_t)length - 2u;
        if (status != DIC_STATUS_OK)
            return status;
    }
    return DIC_J2K_FORMAT_ERROR;
}

static void dic_j2k_decode_tagtree_init(dic_j2k_decode_tagtree *tree)
{
    DIC_J2K_DEBUG_ENTER();
    tree->levels = NULL;
    tree->nodes = NULL;
    tree->level_count = 0u;
    tree->node_count = 0u;
}

static void dic_j2k_decode_tagtree_free(dic_j2k_decode_tagtree *tree)
{
    DIC_J2K_DEBUG_ENTER();
    if (tree == NULL)
        return;
    free(tree->levels);
    free(tree->nodes);
    dic_j2k_decode_tagtree_init(tree);
}

static dic_status dic_j2k_decode_tagtree_alloc(dic_j2k_decode_tagtree *tree, int width, int height)
{
    DIC_J2K_DEBUG_ENTER();
    int current_width;
    int current_height;
    size_t levels = 0u;
    size_t nodes = 0u;
    size_t level;

    if (tree == NULL || width <= 0 || height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    dic_j2k_decode_tagtree_free(tree);
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
    tree->levels = (dic_j2k_decode_tagtree_level *)calloc(levels, sizeof(tree->levels[0]));
    tree->nodes = (dic_j2k_decode_tagtree_node *)calloc(nodes, sizeof(tree->nodes[0]));
    if (tree->levels == NULL || tree->nodes == NULL)
    {
        dic_j2k_decode_tagtree_free(tree);
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

static size_t dic_j2k_decode_tagtree_index(
    const dic_j2k_decode_tagtree *tree,
    size_t level,
    int x,
    int y
)
{
    DIC_J2K_DEBUG_ENTER();
    return tree->levels[level].offset
        + (size_t)y * (size_t)tree->levels[level].width
        + (size_t)x;
}

static void dic_j2k_decode_bit_reader_init(
    dic_j2k_decode_bit_reader *reader,
    const uint8_t *data,
    size_t size
)
{
    DIC_J2K_DEBUG_ENTER();
    reader->data = data;
    reader->size = size;
    reader->byte_offset = 0u;
    reader->bit_offset = 0u;
}

static unsigned int dic_j2k_decode_reader_bits_in_current(const dic_j2k_decode_bit_reader *reader)
{
    DIC_J2K_DEBUG_ENTER();
    return reader->byte_offset > 0u && reader->data[reader->byte_offset - 1u] == 0xffu ? 7u : 8u;
}

static dic_status dic_j2k_decode_read_packet_bit(
    dic_j2k_decode_bit_reader *reader,
    uint32_t *bit
)
{
    DIC_J2K_DEBUG_ENTER();
    unsigned int bits_in_current;
    unsigned int shift;

    if (reader == NULL || bit == NULL || reader->byte_offset >= reader->size)
        return DIC_J2K_FORMAT_ERROR;
    bits_in_current = dic_j2k_decode_reader_bits_in_current(reader);
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

static dic_status dic_j2k_decode_read_packet_bits(
    dic_j2k_decode_bit_reader *reader,
    uint32_t bit_count,
    uint32_t *value
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t index;
    uint32_t result = 0u;

    if (value == NULL || bit_count > 31u)
        return DIC_STATUS_INVALID_ARGUMENT;
    for (index = 0u; index < bit_count; ++index)
    {
        uint32_t bit;
        dic_status status = dic_j2k_decode_read_packet_bit(reader, &bit);

        if (status != DIC_STATUS_OK)
            return status;
        result = (result << 1) | bit;
    }
    *value = result;
    return DIC_STATUS_OK;
}

static size_t dic_j2k_decode_reader_aligned_offset(const dic_j2k_decode_bit_reader *reader)
{
    DIC_J2K_DEBUG_ENTER();
    return reader->byte_offset + (reader->bit_offset == 0u ? 0u : 1u);
}

static dic_status dic_j2k_decode_tagtree_leaf(
    dic_j2k_decode_tagtree *tree,
    int x,
    int y,
    uint32_t threshold,
    dic_j2k_decode_bit_reader *reader,
    int *known,
    uint32_t *value
)
{
    DIC_J2K_DEBUG_ENTER();
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
        dic_j2k_decode_tagtree_node *node;

        if (node_x >= tree->levels[actual_level].width)
            node_x = tree->levels[actual_level].width - 1;
        if (node_y >= tree->levels[actual_level].height)
            node_y = tree->levels[actual_level].height - 1;
        node = tree->nodes + dic_j2k_decode_tagtree_index(tree, actual_level, node_x, node_y);
        if (node->low < lower_bound)
            node->low = lower_bound;
        if (!node->known)
        {
            while (node->low <= threshold)
            {
                uint32_t bit;
                dic_status status = dic_j2k_decode_read_packet_bit(reader, &bit);

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

static dic_status dic_j2k_decode_tagtree_value(
    dic_j2k_decode_tagtree *tree,
    int x,
    int y,
    dic_j2k_decode_bit_reader *reader,
    uint32_t *value
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t threshold;

    for (threshold = 0u; threshold <= DIC_J2K_DECODE_MAX_TAGTREE_VALUE; ++threshold)
    {
        int known;
        dic_status status = dic_j2k_decode_tagtree_leaf(tree, x, y, threshold, reader, &known, value);

        if (status != DIC_STATUS_OK)
            return status;
        if (known)
            return DIC_STATUS_OK;
    }
    return DIC_J2K_FORMAT_ERROR;
}

static dic_status dic_j2k_decode_coding_passes(
    dic_j2k_decode_bit_reader *reader,
    uint32_t *passes
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t bit;
    uint32_t value;
    dic_status status;

    status = dic_j2k_decode_read_packet_bit(reader, &bit);
    if (status != DIC_STATUS_OK)
        return status;
    if (bit == 0u)
    {
        *passes = 1u;
        return DIC_STATUS_OK;
    }
    status = dic_j2k_decode_read_packet_bit(reader, &bit);
    if (status != DIC_STATUS_OK)
        return status;
    if (bit == 0u)
    {
        *passes = 2u;
        return DIC_STATUS_OK;
    }
    status = dic_j2k_decode_read_packet_bits(reader, 2u, &value);
    if (status != DIC_STATUS_OK)
        return status;
    if (value < 3u)
    {
        *passes = value + 3u;
        return DIC_STATUS_OK;
    }
    status = dic_j2k_decode_read_packet_bits(reader, 5u, &value);
    if (status != DIC_STATUS_OK)
        return status;
    if (value < 31u)
    {
        *passes = value + 6u;
        return DIC_STATUS_OK;
    }
    status = dic_j2k_decode_read_packet_bits(reader, 7u, &value);
    if (status != DIC_STATUS_OK)
        return status;
    *passes = value + 37u;
    return *passes <= 164u ? DIC_STATUS_OK : DIC_J2K_FORMAT_ERROR;
}

static void dic_j2k_decode_contribution_list_free(dic_j2k_decode_contribution_list *list)
{
    DIC_J2K_DEBUG_ENTER();
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

static dic_status dic_j2k_decode_contribution_list_push(
    dic_j2k_decode_contribution_list *list,
    dic_j2k_decode_codeblock *block,
    size_t *segment_lengths,
    uint32_t segment_count,
    size_t total_length
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_decode_contribution *new_items;
    size_t new_capacity;

    if (list == NULL || block == NULL || segment_lengths == NULL || segment_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (list->count == list->capacity)
    {
        new_capacity = list->capacity == 0u ? 8u : list->capacity * 2u;
        if (new_capacity < list->capacity)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_items = (dic_j2k_decode_contribution *)realloc(
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

static dic_status dic_j2k_decode_stream_append(
    dic_j2k_codeblock_stream *stream,
    const uint8_t *codeword,
    size_t codeword_size,
    const size_t *segment_lengths,
    uint32_t segment_count
)
{
    DIC_J2K_DEBUG_ENTER();
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

static dic_status dic_j2k_decode_packet(
    const uint8_t *payload,
    size_t payload_size,
    size_t *offset,
    dic_j2k_decode_subband *subbands,
    size_t subband_count,
    uint16_t layer_index,
    const dic_j2k_basic_params *params,
    uint16_t packet_sequence
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_decode_bit_reader reader;
    dic_j2k_decode_contribution_list contributions = {0};
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

    dic_j2k_decode_bit_reader_init(&reader, payload + *offset, payload_size - *offset);
    status = dic_j2k_decode_read_packet_bit(&reader, &nonempty);
    if (status != DIC_STATUS_OK)
        return status;
    if (nonempty)
    {
        size_t subband_index;

        for (subband_index = 0u; subband_index < subband_count && status == DIC_STATUS_OK; ++subband_index)
        {
            dic_j2k_decode_subband *subband = subbands + subband_index;
            int by;

            for (by = 0; by < subband->blocks_y && status == DIC_STATUS_OK; ++by)
            {
                int bx;

                for (bx = 0; bx < subband->blocks_x && status == DIC_STATUS_OK; ++bx)
                {
                    size_t block_index = (size_t)by * (size_t)subband->blocks_x + (size_t)bx;
                    dic_j2k_decode_codeblock *block = subband->blocks + block_index;
                    int first_inclusion = block->stream.coding_passes == 0u;
                    uint32_t included = 0u;

                    if (first_inclusion)
                    {
                        int known;
                        uint32_t value;

                        status = dic_j2k_decode_tagtree_leaf(
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
                        status = dic_j2k_decode_read_packet_bit(&reader, &included);
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

                            status = dic_j2k_decode_tagtree_value(
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
                        status = dic_j2k_decode_coding_passes(&reader, &pass_count);
                        if (status != DIC_STATUS_OK)
                            break;
                        do
                        {
                            uint32_t bit;

                            status = dic_j2k_decode_read_packet_bit(&reader, &bit);
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

                            status = dic_j2k_decode_read_packet_bits(&reader, block->lblock, &length_value);
                            if (status != DIC_STATUS_OK)
                                break;
                            segment_lengths[pass] = length_value;
                            total_length += length_value;
                        }
                        if (status == DIC_STATUS_OK)
                        {
                            status = dic_j2k_decode_contribution_list_push(
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
        header_size = dic_j2k_decode_reader_aligned_offset(&reader);
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
        dic_j2k_decode_contribution *contribution = contributions.items + index;

        if (contribution->total_length > payload_size - *offset)
        {
            status = DIC_J2K_FORMAT_ERROR;
            break;
        }
        status = dic_j2k_decode_stream_append(
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

    dic_j2k_decode_contribution_list_free(&contributions);
    return status;
}

static dic_j2k_subband_orientation dic_j2k_decode_orientation(dic_subband_orientation orientation)
{
    DIC_J2K_DEBUG_ENTER();
    if (orientation == DIC_SUBBAND_HL)
        return DIC_J2K_SUBBAND_HL;
    if (orientation == DIC_SUBBAND_HH)
        return DIC_J2K_SUBBAND_HH;
    return DIC_J2K_SUBBAND_LL_LH;
}

static void dic_j2k_decode_subband_free(dic_j2k_decode_subband *subband)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;
    size_t count;

    if (subband == NULL)
        return;
    count = (size_t)subband->blocks_x * (size_t)subband->blocks_y;
    for (index = 0u; index < count; ++index)
        dic_j2k_codeblock_stream_free(&subband->blocks[index].stream);
    free(subband->blocks);
    dic_j2k_decode_tagtree_free(&subband->inclusion_tree);
    dic_j2k_decode_tagtree_free(&subband->zero_tree);
}

static void dic_j2k_decode_subbands_free(dic_j2k_decode_subband *subbands, size_t count)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;

    if (subbands == NULL)
        return;
    for (index = 0u; index < count; ++index)
        dic_j2k_decode_subband_free(subbands + index);
    free(subbands);
}

static dic_status dic_j2k_decode_init_subband_blocks(dic_j2k_decode_subband *subband)
{
    DIC_J2K_DEBUG_ENTER();
    int by;
    dic_status status;

    subband->blocks_x = (subband->rect.width + DIC_J2K_DECODE_CODEBLOCK_SIZE - 1) / DIC_J2K_DECODE_CODEBLOCK_SIZE;
    subband->blocks_y = (subband->rect.height + DIC_J2K_DECODE_CODEBLOCK_SIZE - 1) / DIC_J2K_DECODE_CODEBLOCK_SIZE;
    subband->blocks = (dic_j2k_decode_codeblock *)calloc(
        (size_t)subband->blocks_x * (size_t)subband->blocks_y,
        sizeof(subband->blocks[0])
    );
    if (subband->blocks == NULL)
        return DIC_STATUS_MEMORY_ERROR;
    status = dic_j2k_decode_tagtree_alloc(&subband->inclusion_tree, subband->blocks_x, subband->blocks_y);
    if (status == DIC_STATUS_OK)
        status = dic_j2k_decode_tagtree_alloc(&subband->zero_tree, subband->blocks_x, subband->blocks_y);
    if (status != DIC_STATUS_OK)
        return status;
    for (by = 0; by < subband->blocks_y; ++by)
    {
        int bx;

        for (bx = 0; bx < subband->blocks_x; ++bx)
        {
            size_t block_index = (size_t)by * (size_t)subband->blocks_x + (size_t)bx;
            int remaining_width = subband->rect.width - bx * DIC_J2K_DECODE_CODEBLOCK_SIZE;
            int remaining_height = subband->rect.height - by * DIC_J2K_DECODE_CODEBLOCK_SIZE;
            dic_j2k_decode_codeblock *block = subband->blocks + block_index;

            dic_j2k_codeblock_stream_init(&block->stream);
            block->lblock = DIC_J2K_DECODE_INITIAL_LBLOCK;
            block->stream.width = (uint32_t)(remaining_width < DIC_J2K_DECODE_CODEBLOCK_SIZE
                ? remaining_width
                : DIC_J2K_DECODE_CODEBLOCK_SIZE);
            block->stream.height = (uint32_t)(remaining_height < DIC_J2K_DECODE_CODEBLOCK_SIZE
                ? remaining_height
                : DIC_J2K_DECODE_CODEBLOCK_SIZE);
            block->stream.subband_orientation = (uint8_t)subband->orientation;
        }
    }
    return DIC_STATUS_OK;
}

static dic_status dic_j2k_decode_make_subbands(
    int width,
    int height,
    int components,
    int levels,
    dic_j2k_decode_subband **subbands_out,
    size_t *count_out
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t per_component = 1u + (size_t)levels * 3u;
    size_t count = (size_t)components * per_component;
    dic_j2k_decode_subband *subbands;
    size_t out = 0u;
    int component;
    dic_status status = DIC_STATUS_OK;

    if (subbands_out == NULL || count_out == NULL || width <= 0 || height <= 0 || components <= 0 || levels < 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    subbands = (dic_j2k_decode_subband *)calloc(count, sizeof(subbands[0]));
    if (subbands == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    for (component = 0; component < components && status == DIC_STATUS_OK; ++component)
    {
        uint32_t extra_bits = components == 3 && component > 0 ? 1u : 0u;
        int resolution;

        dic_j2k_decode_tagtree_init(&subbands[out].inclusion_tree);
        dic_j2k_decode_tagtree_init(&subbands[out].zero_tree);
        subbands[out].component = component;
        subbands[out].orientation = DIC_J2K_SUBBAND_LL_LH;
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
            status = dic_subband_lowest_ll_rect(width, height, levels, &subbands[out].rect);
        }
        if (status == DIC_STATUS_OK)
            status = dic_j2k_decode_init_subband_blocks(subbands + out);
        ++out;

        for (resolution = 1; resolution <= levels && status == DIC_STATUS_OK; ++resolution)
        {
            static const dic_subband_orientation orientations[] = {
                DIC_SUBBAND_HL,
                DIC_SUBBAND_LH,
                DIC_SUBBAND_HH
            };
            size_t orientation_index;
            int level = levels - resolution + 1;

            for (orientation_index = 0u; orientation_index < 3u && status == DIC_STATUS_OK; ++orientation_index)
            {
                dic_j2k_decode_tagtree_init(&subbands[out].inclusion_tree);
                dic_j2k_decode_tagtree_init(&subbands[out].zero_tree);
                subbands[out].component = component;
                subbands[out].orientation = dic_j2k_decode_orientation(orientations[orientation_index]);
                subbands[out].nominal_bitplanes = (orientations[orientation_index] == DIC_SUBBAND_HH ? 11u : 10u)
                    + extra_bits;
                status = dic_subband_rect(width, height, levels, level, orientations[orientation_index], &subbands[out].rect);
                if (status == DIC_STATUS_OK)
                    status = dic_j2k_decode_init_subband_blocks(subbands + out);
                ++out;
            }
        }
    }
    if (status != DIC_STATUS_OK)
    {
        dic_j2k_decode_subbands_free(subbands, count);
        return status;
    }
    *subbands_out = subbands;
    *count_out = count;
    return DIC_STATUS_OK;
}

static dic_j2k_decode_subband *dic_j2k_decode_packet_subbands(
    dic_j2k_decode_subband *subbands,
    size_t subband_count,
    int component,
    int levels,
    int resolution,
    size_t *count
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t per_component = 1u + (size_t)levels * 3u;
    size_t start = (size_t)component * per_component + (resolution == 0 ? 0u : 1u + (size_t)(resolution - 1) * 3u);

    if (start >= subband_count)
        return NULL;
    *count = resolution == 0 ? 1u : 3u;
    return subbands + start;
}

static dic_status dic_j2k_decode_subbands_to_planes(
    dic_j2k_decode_subband *subbands,
    size_t subband_count,
    int32_t *planes,
    int width,
    int height,
    int components
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t subband_index;
    size_t plane_samples = (size_t)width * (size_t)height;

    for (subband_index = 0u; subband_index < subband_count; ++subband_index)
    {
        dic_j2k_decode_subband *subband = subbands + subband_index;
        size_t block_count = (size_t)subband->blocks_x * (size_t)subband->blocks_y;
        size_t block_index;

        for (block_index = 0u; block_index < block_count; ++block_index)
        {
            dic_j2k_decode_codeblock *block = subband->blocks + block_index;
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
            status = dic_j2k_ebcot_decode_codeblock_rect(
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
                int dst_x = subband->rect.x + bx * DIC_J2K_DECODE_CODEBLOCK_SIZE;
                int dst_y = subband->rect.y + by * DIC_J2K_DECODE_CODEBLOCK_SIZE + (int)y;
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

static uint8_t dic_j2k_decode_unshift_u8(int32_t value)
{
    DIC_J2K_DEBUG_ENTER();
    int32_t shifted = value + 128;

    if (shifted < 0)
        return 0u;
    if (shifted > 255)
        return 255u;
    return (uint8_t)shifted;
}

static dic_status dic_j2k_decode_tile_payload(
    const uint8_t *payload,
    size_t payload_size,
    const dic_j2k_basic_params *params,
    uint16_t max_layers,
    int tile_width,
    int tile_height,
    dic_image_u8 *tile
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_decode_subband *subbands = NULL;
    size_t subband_count = 0u;
    int32_t *planes = NULL;
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
    status = dic_j2k_decode_make_subbands(
        tile_width,
        tile_height,
        (int)params->components,
        (int)params->decomposition_levels,
        &subbands,
        &subband_count
    );
    for (layer = 0u; status == DIC_STATUS_OK && layer < layers_to_decode; ++layer)
    {
        int resolution;

        for (resolution = 0; status == DIC_STATUS_OK && resolution <= (int)params->decomposition_levels; ++resolution)
        {
            for (component = 0; status == DIC_STATUS_OK && component < (int)params->components; ++component)
            {
                size_t packet_subband_count = 0u;
                dic_j2k_decode_subband *packet_subbands = dic_j2k_decode_packet_subbands(
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
                status = dic_j2k_decode_packet(
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
        status = dic_j2k_decode_subbands_to_planes(
            subbands,
            subband_count,
            planes,
            tile_width,
            tile_height,
            (int)params->components
        );
    for (component = 0; status == DIC_STATUS_OK && component < (int)params->components; ++component)
    {
        status = dic_dwt53_inverse_plane(
            planes + (size_t)component * plane_samples,
            tile_width,
            tile_height,
            (int)params->decomposition_levels
        );
    }
    if (status == DIC_STATUS_OK)
        status = dic_image_u8_alloc(tile, tile_width, tile_height, (int)params->components);
    if (status == DIC_STATUS_OK && params->components == 3u && params->multiple_component_transform)
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
            status = dic_j2k_rct_inverse(interleaved, plane_samples);
            if (status == DIC_STATUS_OK)
            {
                for (pixel = 0u; pixel < plane_samples; ++pixel)
                {
                    tile->data[pixel * 3u + 0u] = dic_j2k_decode_unshift_u8(interleaved[pixel * 3u + 0u]);
                    tile->data[pixel * 3u + 1u] = dic_j2k_decode_unshift_u8(interleaved[pixel * 3u + 1u]);
                    tile->data[pixel * 3u + 2u] = dic_j2k_decode_unshift_u8(interleaved[pixel * 3u + 2u]);
                }
            }
            free(interleaved);
        }
    }
    else if (status == DIC_STATUS_OK)
    {
        size_t sample;

        for (sample = 0u; sample < plane_samples * (size_t)params->components; ++sample)
            tile->data[sample] = dic_j2k_decode_unshift_u8(planes[sample]);
    }

    free(planes);
    dic_j2k_decode_subbands_free(subbands, subband_count);
    return status;
}

static const dic_j2k_decode_tile_part *dic_j2k_decode_find_tile(
    const dic_j2k_decode_tile_list *tiles,
    uint16_t tile_index
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;

    for (index = 0u; index < tiles->count; ++index)
    {
        if (tiles->parts[index].tile_index == tile_index)
            return tiles->parts + index;
    }
    return NULL;
}

static dic_status dic_j2k_decode_image_from_codestream(
    const uint8_t *data,
    size_t size,
    uint16_t max_layers,
    dic_image_u8 *image
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_basic_params params;
    dic_j2k_decode_tile_list tiles = {0};
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t tiles_x;
    uint32_t tiles_y;
    uint32_t tile_index = 0u;
    dic_status status;

    if (data == NULL || image == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    status = dic_j2k_decode_parse_codestream(data, size, &params, &tiles);
    if (status != DIC_STATUS_OK)
    {
        dic_j2k_decode_tile_list_free(&tiles);
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
        const dic_j2k_decode_tile_part *part = dic_j2k_decode_find_tile(&tiles, (uint16_t)tile_index);
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
        status = dic_j2k_decode_tile_payload(
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
    dic_j2k_decode_tile_list_free(&tiles);
    return status;
}

dic_status dic_j2k_read_image_codestream(const char *path, dic_image_u8 *image)
{
    return dic_j2k_read_image_codestream_layers(path, 0u, image);
}

dic_status dic_j2k_read_image_codestream_layers(const char *path, uint16_t max_layers, dic_image_u8 *image)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_decode_buffer buffer;
    dic_status status;

    if (path == NULL || image == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    status = dic_j2k_decode_read_file(path, &buffer);
    if (status == DIC_STATUS_OK)
        status = dic_j2k_decode_image_from_codestream(buffer.data, buffer.size, max_layers, image);
    dic_j2k_decode_buffer_free(&buffer);
    return status;
}

dic_status dic_j2k_read_image_jp2(const char *path, dic_image_u8 *image)
{
    return dic_j2k_read_image_jp2_layers(path, 0u, image);
}

dic_status dic_j2k_read_image_jp2_layers(const char *path, uint16_t max_layers, dic_image_u8 *image)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_decode_buffer buffer;
    dic_j2k_decode_view view;
    dic_status status;

    if (path == NULL || image == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    status = dic_j2k_decode_read_file(path, &buffer);
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

        if (!dic_j2k_decode_read_u32_be(&view, &length) || !dic_j2k_decode_read_u32_be(&view, &type))
            break;
        payload_start = view.offset;
        if (length == 1u || (length != 0u && length < 8u))
            break;
        box_size = length == 0u ? view.size - box_start : (size_t)length;
        if (box_size < 8u || box_size > view.size - box_start)
            break;
        if (type == DIC_JP2_BOX_JP2C)
        {
            status = dic_j2k_decode_image_from_codestream(
                buffer.data + payload_start,
                box_size - 8u,
                max_layers,
                image
            );
            break;
        }
        view.offset = box_start + box_size;
    }
    dic_j2k_decode_buffer_free(&buffer);
    return status;
}

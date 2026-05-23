/**
 * @file dic_j2k_packet.c
 * @brief Builds JPEG 2000 packet headers and packet payloads according to T.800 Annex B.
 *
 * This implementation packs packet-header bits with Annex B byte-stuffing rules, encodes
 * inclusion and zero-bitplane tag-trees, computes Lblock increments, selects code-block
 * truncation prefixes from EBCOT rate-distortion slopes, and appends code-block
 * contributions. Packet sub-band payloads may select a precinct-sized window from the
 * complete sub-band code-block grid, leaving progression-order scheduling to callers.
 *
 * References: dic_j2k_ebcot.h for code-block contribution metadata, dic_j2k_codestream.c
 * for tile-part emission, dic_j2k_tagtree.c for tag-tree helpers, and Annex J.11 packet
 * header decoding examples.
 */

#include "j2k/dic_j2k_packet.h"
#include "j2k/dic_j2k_debug.h"

#include <stdlib.h>
#include <string.h>

typedef struct dic_j2k_packet_tagtree_level
{
    int width;
    int height;
    size_t offset;
} dic_j2k_packet_tagtree_level;

typedef struct dic_j2k_packet_tagtree_node
{
    uint32_t value;
    uint32_t low;
    int known;
} dic_j2k_packet_tagtree_node;

typedef struct dic_j2k_packet_tagtree
{
    dic_j2k_packet_tagtree_level *levels;
    dic_j2k_packet_tagtree_node *nodes;
    size_t level_count;
    size_t node_count;
} dic_j2k_packet_tagtree;

typedef struct dic_j2k_packet_parse_codeblock_state
{
    uint32_t coding_passes;
    uint32_t lblock;
    uint32_t zero_bitplanes;
} dic_j2k_packet_parse_codeblock_state;

typedef struct dic_j2k_packet_parse_subband_state
{
    int blocks_x;
    int blocks_y;
    int first_block_x;
    int first_block_y;
    int total_blocks_x;
    dic_j2k_packet_parse_codeblock_state *codeblocks;
    dic_j2k_packet_tagtree inclusion_tree;
    dic_j2k_packet_tagtree zero_tree;
} dic_j2k_packet_parse_subband_state;

struct dic_j2k_packet_header_parser
{
    dic_j2k_packet_parse_subband_state *subbands;
    size_t subband_count;
    int terminated_passes;
};

typedef struct dic_j2k_packet_bit_reader
{
    const uint8_t *data;
    size_t size;
    size_t byte_offset;
    unsigned int bit_offset;
} dic_j2k_packet_bit_reader;

typedef struct dic_j2k_packet_pending_range
{
    size_t range_index;
    size_t length;
} dic_j2k_packet_pending_range;

typedef struct dic_j2k_packet_pending_range_list
{
    dic_j2k_packet_pending_range *items;
    size_t count;
    size_t capacity;
} dic_j2k_packet_pending_range_list;

static int dic_j2k_packet_active_blocks_x(
    int full_blocks_x,
    int packet_blocks_x
)
{
    return packet_blocks_x > 0 ? packet_blocks_x : full_blocks_x;
}

static int dic_j2k_packet_active_blocks_y(
    int full_blocks_y,
    int packet_blocks_y
)
{
    return packet_blocks_y > 0 ? packet_blocks_y : full_blocks_y;
}

static dic_status dic_j2k_packet_validate_window(
    int full_blocks_x,
    int full_blocks_y,
    int first_block_x,
    int first_block_y,
    int packet_blocks_x,
    int packet_blocks_y
)
{
    if (full_blocks_x <= 0 || full_blocks_y <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (first_block_x < 0 || first_block_y < 0 || packet_blocks_x <= 0 || packet_blocks_y <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (first_block_x > full_blocks_x || first_block_y > full_blocks_y)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (packet_blocks_x > full_blocks_x - first_block_x || packet_blocks_y > full_blocks_y - first_block_y)
        return DIC_STATUS_INVALID_ARGUMENT;
    return DIC_STATUS_OK;
}

static dic_status dic_j2k_packet_payload_window(
    const dic_j2k_packet_subband_payload *subband,
    int *first_block_x,
    int *first_block_y,
    int *packet_blocks_x,
    int *packet_blocks_y
)
{
    if (subband == NULL || first_block_x == NULL || first_block_y == NULL
        || packet_blocks_x == NULL || packet_blocks_y == NULL)
    {
        return DIC_STATUS_INVALID_ARGUMENT;
    }

    *first_block_x = subband->first_block_x;
    *first_block_y = subband->first_block_y;
    *packet_blocks_x = dic_j2k_packet_active_blocks_x(subband->blocks_x, subband->packet_blocks_x);
    *packet_blocks_y = dic_j2k_packet_active_blocks_y(subband->blocks_y, subband->packet_blocks_y);
    return dic_j2k_packet_validate_window(
        subband->blocks_x,
        subband->blocks_y,
        *first_block_x,
        *first_block_y,
        *packet_blocks_x,
        *packet_blocks_y
    );
}

static dic_status dic_j2k_packet_layout_window(
    const dic_j2k_packet_subband_layout *subband,
    int *first_block_x,
    int *first_block_y,
    int *packet_blocks_x,
    int *packet_blocks_y
)
{
    if (subband == NULL || first_block_x == NULL || first_block_y == NULL
        || packet_blocks_x == NULL || packet_blocks_y == NULL)
    {
        return DIC_STATUS_INVALID_ARGUMENT;
    }

    *first_block_x = subband->first_block_x;
    *first_block_y = subband->first_block_y;
    *packet_blocks_x = dic_j2k_packet_active_blocks_x(subband->blocks_x, subband->packet_blocks_x);
    *packet_blocks_y = dic_j2k_packet_active_blocks_y(subband->blocks_y, subband->packet_blocks_y);
    return dic_j2k_packet_validate_window(
        subband->blocks_x,
        subband->blocks_y,
        *first_block_x,
        *first_block_y,
        *packet_blocks_x,
        *packet_blocks_y
    );
}

static size_t dic_j2k_packet_payload_stream_index(
    const dic_j2k_packet_subband_payload *subband,
    int local_block_x,
    int local_block_y
)
{
    return (size_t)(subband->first_block_y + local_block_y) * (size_t)subband->blocks_x
        + (size_t)(subband->first_block_x + local_block_x);
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.1, packet header bits are packed MSB-first with stuffing after 0xFF. */
void dic_j2k_packet_header_init(dic_j2k_packet_header *header)
{
    DIC_J2K_DEBUG_ENTER();
    if (header == NULL)
        return;
    header->data = NULL;
    header->size = 0u;
    header->capacity = 0u;
    header->current_byte = 0u;
    header->bits_used = 0u;
    header->bits_available = 8u;
    header->finished = 0;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10, packet headers are byte buffers preceding packet bodies. */
void dic_j2k_packet_header_free(dic_j2k_packet_header *header)
{
    DIC_J2K_DEBUG_ENTER();
    if (header == NULL)
        return;
    free(header->data);
    dic_j2k_packet_header_init(header);
}

void dic_j2k_packet_parse_result_init(dic_j2k_packet_parse_result *result)
{
    DIC_J2K_DEBUG_ENTER();
    if (result == NULL)
        return;
    result->ranges = NULL;
    result->range_count = 0u;
    result->range_capacity = 0u;
    result->packet_header_size = 0u;
    result->packet_body_size = 0u;
    result->is_empty = 0;
}

void dic_j2k_packet_parse_result_free(dic_j2k_packet_parse_result *result)
{
    DIC_J2K_DEBUG_ENTER();
    if (result == NULL)
        return;
    free(result->ranges);
    dic_j2k_packet_parse_result_init(result);
}

static dic_status dic_j2k_packet_parse_result_push_range(
    dic_j2k_packet_parse_result *result,
    size_t subband_index,
    size_t codeblock_index,
    uint32_t pass_index,
    size_t byte_count,
    size_t *range_index
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_packet_pass_range *new_ranges;
    size_t new_capacity;

    if (result == NULL || range_index == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (result->range_count == result->range_capacity)
    {
        new_capacity = result->range_capacity == 0u ? 8u : result->range_capacity * 2u;
        if (new_capacity < result->range_capacity)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_ranges = (dic_j2k_packet_pass_range *)realloc(
            result->ranges,
            new_capacity * sizeof(result->ranges[0])
        );
        if (new_ranges == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        result->ranges = new_ranges;
        result->range_capacity = new_capacity;
    }
    result->ranges[result->range_count].subband_index = subband_index;
    result->ranges[result->range_count].codeblock_index = codeblock_index;
    result->ranges[result->range_count].pass_index = pass_index;
    result->ranges[result->range_count].byte_offset = 0u;
    result->ranges[result->range_count].byte_count = byte_count;
    *range_index = result->range_count;
    ++result->range_count;
    return DIC_STATUS_OK;
}

static void dic_j2k_packet_pending_range_list_free(dic_j2k_packet_pending_range_list *list)
{
    DIC_J2K_DEBUG_ENTER();
    if (list == NULL)
        return;
    free(list->items);
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static dic_status dic_j2k_packet_pending_range_list_push(
    dic_j2k_packet_pending_range_list *list,
    size_t range_index,
    size_t length
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_packet_pending_range *new_items;
    size_t new_capacity;

    if (list == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (list->count == list->capacity)
    {
        new_capacity = list->capacity == 0u ? 8u : list->capacity * 2u;
        if (new_capacity < list->capacity)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_items = (dic_j2k_packet_pending_range *)realloc(
            list->items,
            new_capacity * sizeof(list->items[0])
        );
        if (new_items == NULL)
            return DIC_STATUS_MEMORY_ERROR;
        list->items = new_items;
        list->capacity = new_capacity;
    }
    list->items[list->count].range_index = range_index;
    list->items[list->count].length = length;
    ++list->count;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.1, packet headers are emitted as a whole number of bytes. */
static dic_status dic_j2k_packet_reserve(dic_j2k_packet_header *header, size_t additional)
{
    DIC_J2K_DEBUG_ENTER();
    uint8_t *new_data;
    size_t new_capacity;

    if (additional > (size_t)-1 - header->size)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (header->size + additional <= header->capacity)
        return DIC_STATUS_OK;

    new_capacity = header->capacity == 0u ? 16u : header->capacity;
    while (new_capacity < header->size + additional)
    {
        if (new_capacity > (size_t)-1 / 2u)
            return DIC_STATUS_INVALID_ARGUMENT;
        new_capacity *= 2u;
    }

    new_data = (uint8_t *)realloc(header->data, new_capacity);
    if (new_data == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    header->data = new_data;
    header->capacity = new_capacity;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.1, after an emitted 0xFF byte the following byte reserves its MSB as a zero stuff bit. */
static dic_status dic_j2k_packet_emit_current(dic_j2k_packet_header *header)
{
    DIC_J2K_DEBUG_ENTER();
    dic_status status = dic_j2k_packet_reserve(header, 1u);

    if (status != DIC_STATUS_OK)
        return status;

    header->data[header->size++] = header->current_byte;
    if (header->current_byte == 0xffu)
        header->bits_available = 7u;
    else
        header->bits_available = 8u;
    header->current_byte = 0u;
    header->bits_used = 0u;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.1, packet header bits are packed from MSB to LSB. */
dic_status dic_j2k_packet_header_append_bit(
    dic_j2k_packet_header *header,
    unsigned int bit
)
{
    DIC_J2K_DEBUG_ENTER();
    unsigned int bit_position;

    if (header == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (header->finished)
        return DIC_STATUS_INVALID_ARGUMENT;

    bit_position = header->bits_available - 1u - header->bits_used;
    if (bit != 0u)
        header->current_byte |= (uint8_t)(1u << bit_position);

    ++header->bits_used;
    if (header->bits_used == header->bits_available)
        return dic_j2k_packet_emit_current(header);
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.1, the final packet-header byte is padded to a byte boundary and shall not be 0xFF. */
dic_status dic_j2k_packet_header_finish(dic_j2k_packet_header *header)
{
    DIC_J2K_DEBUG_ENTER();
    dic_status status;

    if (header == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (header->finished)
        return DIC_STATUS_OK;

    if (header->bits_used > 0u || header->bits_available == 7u)
    {
        status = dic_j2k_packet_emit_current(header);
        if (status != DIC_STATUS_OK)
            return status;
    }

    header->finished = 1;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.3, the first packet-header bit is zero for an empty packet. */
dic_status dic_j2k_packet_build_empty_header(dic_j2k_packet_header *header)
{
    DIC_J2K_DEBUG_ENTER();
    dic_status status;

    if (header == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_j2k_packet_header_free(header);
    dic_j2k_packet_header_init(header);

    status = dic_j2k_packet_header_append_bit(header, 0u);
    if (status != DIC_STATUS_OK)
        return status;

    return dic_j2k_packet_header_finish(header);
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.5 and B.10.7, several packet fields use unary zero-runs terminated by one. */
dic_status dic_j2k_packet_header_append_unary_zeros_then_one(
    dic_j2k_packet_header *header,
    uint32_t zero_count
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t index;
    dic_status status;

    if (header == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (index = 0u; index < zero_count; ++index)
    {
        status = dic_j2k_packet_header_append_bit(header, 0u);
        if (status != DIC_STATUS_OK)
            return status;
    }

    return dic_j2k_packet_header_append_bit(header, 1u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.7.1, codeword segment length is represented with a fixed number of packet-header bits. */
static dic_status dic_j2k_packet_header_append_bits(
    dic_j2k_packet_header *header,
    uint32_t value,
    uint32_t bit_count
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t bit;
    dic_status status;

    for (bit = 0u; bit < bit_count; ++bit)
    {
        uint32_t shift = bit_count - 1u - bit;

        status = dic_j2k_packet_header_append_bit(header, (value >> shift) & 1u);
        if (status != DIC_STATUS_OK)
            return status;
    }

    return DIC_STATUS_OK;
}

static uint32_t dic_j2k_packet_floor_log2(uint32_t value)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t result = 0u;

    while (value > 1u)
    {
        ++result;
        value >>= 1u;
    }

    return result;
}

static uint32_t dic_j2k_packet_bit_width(uint32_t value)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t bits = 1u;

    while (value > 1u)
    {
        ++bits;
        value >>= 1u;
    }

    return bits;
}

static void dic_j2k_packet_tagtree_init(dic_j2k_packet_tagtree *tree)
{
    DIC_J2K_DEBUG_ENTER();
    tree->levels = NULL;
    tree->nodes = NULL;
    tree->level_count = 0u;
    tree->node_count = 0u;
}

static void dic_j2k_packet_tagtree_free(dic_j2k_packet_tagtree *tree)
{
    DIC_J2K_DEBUG_ENTER();
    free(tree->levels);
    free(tree->nodes);
    dic_j2k_packet_tagtree_init(tree);
}

static dic_status dic_j2k_packet_tagtree_alloc(
    dic_j2k_packet_tagtree *tree,
    int width,
    int height
)
{
    DIC_J2K_DEBUG_ENTER();
    int w = width;
    int h = height;
    size_t levels = 0u;
    size_t nodes = 0u;
    size_t index;

    if (tree == NULL || width <= 0 || height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_j2k_packet_tagtree_free(tree);
    while (w > 0 && h > 0)
    {
        if ((size_t)w > (size_t)-1 / (size_t)h)
            return DIC_STATUS_INVALID_ARGUMENT;
        if (nodes > (size_t)-1 - (size_t)w * (size_t)h)
            return DIC_STATUS_INVALID_ARGUMENT;
        nodes += (size_t)w * (size_t)h;
        ++levels;
        if (w == 1 && h == 1)
            break;
        w = (w + 1) / 2;
        h = (h + 1) / 2;
    }

    tree->levels = (dic_j2k_packet_tagtree_level *)calloc(levels, sizeof(tree->levels[0]));
    tree->nodes = (dic_j2k_packet_tagtree_node *)calloc(nodes, sizeof(tree->nodes[0]));
    if (tree->levels == NULL || tree->nodes == NULL)
    {
        dic_j2k_packet_tagtree_free(tree);
        return DIC_STATUS_MEMORY_ERROR;
    }

    w = width;
    h = height;
    for (index = 0u; index < levels; ++index)
    {
        tree->levels[index].width = w;
        tree->levels[index].height = h;
        tree->levels[index].offset = index == 0u
            ? 0u
            : tree->levels[index - 1u].offset
                + (size_t)tree->levels[index - 1u].width * (size_t)tree->levels[index - 1u].height;
        if (w == 1 && h == 1)
            break;
        w = (w + 1) / 2;
        h = (h + 1) / 2;
    }
    tree->level_count = levels;
    tree->node_count = nodes;
    return DIC_STATUS_OK;
}

static size_t dic_j2k_packet_tagtree_index(
    const dic_j2k_packet_tagtree *tree,
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

static dic_status dic_j2k_packet_tagtree_set_leaf(
    dic_j2k_packet_tagtree *tree,
    int x,
    int y,
    uint32_t value
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;

    if (tree == NULL || tree->nodes == NULL || x < 0 || y < 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (x >= tree->levels[0].width || y >= tree->levels[0].height)
        return DIC_STATUS_INVALID_ARGUMENT;

    index = dic_j2k_packet_tagtree_index(tree, 0u, x, y);
    tree->nodes[index].value = value;
    return DIC_STATUS_OK;
}

static void dic_j2k_packet_tagtree_build_minima(dic_j2k_packet_tagtree *tree)
{
    DIC_J2K_DEBUG_ENTER();
    size_t level;

    for (level = 1u; level < tree->level_count; ++level)
    {
        int y;
        const dic_j2k_packet_tagtree_level *current = tree->levels + level;
        const dic_j2k_packet_tagtree_level *child = tree->levels + level - 1u;

        for (y = 0; y < current->height; ++y)
        {
            int x;

            for (x = 0; x < current->width; ++x)
            {
                uint32_t minimum = UINT32_MAX;
                int dy;

                for (dy = 0; dy < 2; ++dy)
                {
                    int dx;
                    int cy = y * 2 + dy;

                    if (cy >= child->height)
                        continue;
                    for (dx = 0; dx < 2; ++dx)
                    {
                        int cx = x * 2 + dx;
                        uint32_t value;

                        if (cx >= child->width)
                            continue;
                        value = tree->nodes[dic_j2k_packet_tagtree_index(tree, level - 1u, cx, cy)].value;
                        if (value < minimum)
                            minimum = value;
                    }
                }
                tree->nodes[dic_j2k_packet_tagtree_index(tree, level, x, y)].value = minimum;
            }
        }
    }
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.2, each tag-tree node keeps causal state and emits zero tests until the queried threshold is known. */
static dic_status dic_j2k_packet_tagtree_encode_leaf(
    dic_j2k_packet_tagtree *tree,
    int x,
    int y,
    uint32_t threshold,
    dic_j2k_packet_header *header
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t level;
    uint32_t lower_bound = 0u;
    dic_status status;

    if (tree == NULL || header == NULL || tree->nodes == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (x < 0 || y < 0 || x >= tree->levels[0].width || y >= tree->levels[0].height)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (level = tree->level_count; level > 0u; --level)
    {
        size_t actual_level = level - 1u;
        int node_x = x >> actual_level;
        int node_y = y >> actual_level;
        dic_j2k_packet_tagtree_node *node;

        if (node_x >= tree->levels[actual_level].width)
            node_x = tree->levels[actual_level].width - 1;
        if (node_y >= tree->levels[actual_level].height)
            node_y = tree->levels[actual_level].height - 1;

        node = tree->nodes + dic_j2k_packet_tagtree_index(tree, actual_level, node_x, node_y);
        if (node->low < lower_bound)
            node->low = lower_bound;
        if (!node->known)
        {
            while (node->low <= threshold)
            {
                if (node->value > node->low)
                {
                    status = dic_j2k_packet_header_append_bit(header, 0u);
                    if (status != DIC_STATUS_OK)
                        return status;
                    ++node->low;
                }
                else
                {
                    status = dic_j2k_packet_header_append_bit(header, 1u);
                    if (status != DIC_STATUS_OK)
                        return status;
                    node->known = 1;
                    break;
                }
            }
        }
        lower_bound = node->low;
        if (!node->known)
            break;
    }

    return DIC_STATUS_OK;
}

static void dic_j2k_packet_bit_reader_init(
    dic_j2k_packet_bit_reader *reader,
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

static unsigned int dic_j2k_packet_reader_bits_in_current(const dic_j2k_packet_bit_reader *reader)
{
    DIC_J2K_DEBUG_ENTER();
    return reader->byte_offset > 0u && reader->data[reader->byte_offset - 1u] == 0xffu ? 7u : 8u;
}

static dic_status dic_j2k_packet_reader_read_bit(
    dic_j2k_packet_bit_reader *reader,
    uint32_t *bit
)
{
    DIC_J2K_DEBUG_ENTER();
    unsigned int bits_in_current;
    unsigned int shift;

    if (reader == NULL || bit == NULL || reader->byte_offset >= reader->size)
        return DIC_J2K_FORMAT_ERROR;
    bits_in_current = dic_j2k_packet_reader_bits_in_current(reader);
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

static dic_status dic_j2k_packet_reader_read_bits(
    dic_j2k_packet_bit_reader *reader,
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
        dic_status status = dic_j2k_packet_reader_read_bit(reader, &bit);

        if (status != DIC_STATUS_OK)
            return status;
        result = (result << 1) | bit;
    }
    *value = result;
    return DIC_STATUS_OK;
}

static size_t dic_j2k_packet_reader_aligned_offset(const dic_j2k_packet_bit_reader *reader)
{
    DIC_J2K_DEBUG_ENTER();
    return reader->byte_offset + (reader->bit_offset == 0u ? 0u : 1u);
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.2, tag-tree decoding advances node lower bounds until the queried threshold is determined. */
static dic_status dic_j2k_packet_tagtree_decode_leaf(
    dic_j2k_packet_tagtree *tree,
    int x,
    int y,
    uint32_t threshold,
    dic_j2k_packet_bit_reader *reader,
    int *known,
    uint32_t *value
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t level;
    uint32_t lower_bound = 0u;

    if (tree == NULL || reader == NULL || known == NULL || value == NULL || tree->nodes == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (x < 0 || y < 0 || x >= tree->levels[0].width || y >= tree->levels[0].height)
        return DIC_STATUS_INVALID_ARGUMENT;

    *known = 0;
    *value = threshold + 1u;
    for (level = tree->level_count; level > 0u; --level)
    {
        size_t actual_level = level - 1u;
        int node_x = x >> actual_level;
        int node_y = y >> actual_level;
        dic_j2k_packet_tagtree_node *node;

        if (node_x >= tree->levels[actual_level].width)
            node_x = tree->levels[actual_level].width - 1;
        if (node_y >= tree->levels[actual_level].height)
            node_y = tree->levels[actual_level].height - 1;
        node = tree->nodes + dic_j2k_packet_tagtree_index(tree, actual_level, node_x, node_y);
        if (node->low < lower_bound)
            node->low = lower_bound;
        if (!node->known)
        {
            while (node->low <= threshold)
            {
                uint32_t bit;
                dic_status status = dic_j2k_packet_reader_read_bit(reader, &bit);

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

static dic_status dic_j2k_packet_tagtree_decode_value(
    dic_j2k_packet_tagtree *tree,
    int x,
    int y,
    dic_j2k_packet_bit_reader *reader,
    uint32_t *value
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t threshold;

    if (value == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    for (threshold = 0u; threshold <= 64u; ++threshold)
    {
        int known;
        dic_status status = dic_j2k_packet_tagtree_decode_leaf(
            tree,
            x,
            y,
            threshold,
            reader,
            &known,
            value
        );

        if (status != DIC_STATUS_OK)
            return status;
        if (known)
            return DIC_STATUS_OK;
    }
    return DIC_J2K_FORMAT_ERROR;
}

static dic_status dic_j2k_packet_header_parse_coding_passes(
    dic_j2k_packet_bit_reader *reader,
    uint32_t *passes
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t bit;
    uint32_t value;
    dic_status status;

    status = dic_j2k_packet_reader_read_bit(reader, &bit);
    if (status != DIC_STATUS_OK)
        return status;
    if (bit == 0u)
    {
        *passes = 1u;
        return DIC_STATUS_OK;
    }
    status = dic_j2k_packet_reader_read_bit(reader, &bit);
    if (status != DIC_STATUS_OK)
        return status;
    if (bit == 0u)
    {
        *passes = 2u;
        return DIC_STATUS_OK;
    }
    status = dic_j2k_packet_reader_read_bits(reader, 2u, &value);
    if (status != DIC_STATUS_OK)
        return status;
    if (value < 3u)
    {
        *passes = value + 3u;
        return DIC_STATUS_OK;
    }
    status = dic_j2k_packet_reader_read_bits(reader, 5u, &value);
    if (status != DIC_STATUS_OK)
        return status;
    if (value < 31u)
    {
        *passes = value + 6u;
        return DIC_STATUS_OK;
    }
    status = dic_j2k_packet_reader_read_bits(reader, 7u, &value);
    if (status != DIC_STATUS_OK)
        return status;
    *passes = value + 37u;
    return *passes <= 164u ? DIC_STATUS_OK : DIC_J2K_FORMAT_ERROR;
}

static void dic_j2k_packet_parse_subband_state_free(dic_j2k_packet_parse_subband_state *subband)
{
    DIC_J2K_DEBUG_ENTER();
    if (subband == NULL)
        return;
    free(subband->codeblocks);
    dic_j2k_packet_tagtree_free(&subband->inclusion_tree);
    dic_j2k_packet_tagtree_free(&subband->zero_tree);
    subband->codeblocks = NULL;
    subband->blocks_x = 0;
    subband->blocks_y = 0;
}

void dic_j2k_packet_header_parser_destroy(dic_j2k_packet_header_parser *parser)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;

    if (parser == NULL)
        return;
    for (index = 0u; index < parser->subband_count; ++index)
        dic_j2k_packet_parse_subband_state_free(parser->subbands + index);
    free(parser->subbands);
    free(parser);
}

dic_status dic_j2k_packet_header_parser_create(
    const dic_j2k_packet_subband_layout *subbands,
    size_t subband_count,
    int terminated_passes,
    dic_j2k_packet_header_parser **parser
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_packet_header_parser *created;
    size_t subband_index;
    dic_status status = DIC_STATUS_OK;

    if (parser == NULL || subbands == NULL || subband_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;
    *parser = NULL;
    created = (dic_j2k_packet_header_parser *)calloc(1u, sizeof(created[0]));
    if (created == NULL)
        return DIC_STATUS_MEMORY_ERROR;
    created->subbands = (dic_j2k_packet_parse_subband_state *)calloc(
        subband_count,
        sizeof(created->subbands[0])
    );
    if (created->subbands == NULL)
    {
        free(created);
        return DIC_STATUS_MEMORY_ERROR;
    }
    created->subband_count = subband_count;
    created->terminated_passes = terminated_passes != 0;

    for (subband_index = 0u; subband_index < subband_count; ++subband_index)
    {
        dic_j2k_packet_parse_subband_state *state = created->subbands + subband_index;
        size_t codeblock_count;
        size_t codeblock_index;
        int first_block_x;
        int first_block_y;
        int packet_blocks_x;
        int packet_blocks_y;

        dic_j2k_packet_tagtree_init(&state->inclusion_tree);
        dic_j2k_packet_tagtree_init(&state->zero_tree);
        status = dic_j2k_packet_layout_window(
            subbands + subband_index,
            &first_block_x,
            &first_block_y,
            &packet_blocks_x,
            &packet_blocks_y
        );
        if (status != DIC_STATUS_OK)
            break;
        if ((size_t)packet_blocks_x > (size_t)-1 / (size_t)packet_blocks_y)
        {
            status = DIC_STATUS_INVALID_ARGUMENT;
            break;
        }
        state->blocks_x = packet_blocks_x;
        state->blocks_y = packet_blocks_y;
        state->first_block_x = first_block_x;
        state->first_block_y = first_block_y;
        state->total_blocks_x = subbands[subband_index].blocks_x;
        codeblock_count = (size_t)state->blocks_x * (size_t)state->blocks_y;
        state->codeblocks = (dic_j2k_packet_parse_codeblock_state *)calloc(
            codeblock_count,
            sizeof(state->codeblocks[0])
        );
        if (state->codeblocks == NULL)
        {
            status = DIC_STATUS_MEMORY_ERROR;
            break;
        }
        for (codeblock_index = 0u; codeblock_index < codeblock_count; ++codeblock_index)
            state->codeblocks[codeblock_index].lblock = 3u;
        status = dic_j2k_packet_tagtree_alloc(&state->inclusion_tree, state->blocks_x, state->blocks_y);
        if (status == DIC_STATUS_OK)
            status = dic_j2k_packet_tagtree_alloc(&state->zero_tree, state->blocks_x, state->blocks_y);
        if (status != DIC_STATUS_OK)
            break;
    }
    if (status != DIC_STATUS_OK)
    {
        dic_j2k_packet_header_parser_destroy(created);
        return status;
    }
    *parser = created;
    return DIC_STATUS_OK;
}

static dic_status dic_j2k_packet_header_parser_parse_codeblock(
    dic_j2k_packet_header_parser *parser,
    dic_j2k_packet_parse_subband_state *subband,
    size_t subband_index,
    int bx,
    int by,
    uint16_t layer_index,
    dic_j2k_packet_bit_reader *reader,
    dic_j2k_packet_parse_result *result,
    dic_j2k_packet_pending_range_list *pending
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t codeblock_index = (size_t)by * (size_t)subband->blocks_x + (size_t)bx;
    size_t absolute_codeblock_index = (size_t)(subband->first_block_y + by) * (size_t)subband->total_blocks_x
        + (size_t)(subband->first_block_x + bx);
    dic_j2k_packet_parse_codeblock_state *codeblock = subband->codeblocks + codeblock_index;
    int first_inclusion = codeblock->coding_passes == 0u;
    uint32_t included = 0u;
    dic_status status;

    if (first_inclusion)
    {
        int known;
        uint32_t value;

        status = dic_j2k_packet_tagtree_decode_leaf(
            &subband->inclusion_tree,
            bx,
            by,
            layer_index,
            reader,
            &known,
            &value
        );
        if (status != DIC_STATUS_OK)
            return status;
        included = (uint32_t)(known && value <= layer_index);
    }
    else
    {
        status = dic_j2k_packet_reader_read_bit(reader, &included);
        if (status != DIC_STATUS_OK)
            return status;
    }
    if (!included)
        return DIC_STATUS_OK;
    if (first_inclusion)
    {
        status = dic_j2k_packet_tagtree_decode_value(
            &subband->zero_tree,
            bx,
            by,
            reader,
            &codeblock->zero_bitplanes
        );
        if (status != DIC_STATUS_OK)
            return status;
    }
    {
        uint32_t pass_count;
        uint32_t lblock_increment = 0u;
        uint32_t pass;

        status = dic_j2k_packet_header_parse_coding_passes(reader, &pass_count);
        if (status != DIC_STATUS_OK)
            return status;
        do
        {
            uint32_t bit;

            status = dic_j2k_packet_reader_read_bit(reader, &bit);
            if (status != DIC_STATUS_OK)
                return status;
            if (bit == 0u)
                break;
            ++lblock_increment;
        } while (lblock_increment < 32u);
        if (lblock_increment >= 32u)
            return DIC_J2K_FORMAT_ERROR;
        codeblock->lblock += lblock_increment;
        if (codeblock->lblock > 31u)
            return DIC_J2K_FORMAT_ERROR;

        if (!parser->terminated_passes && pass_count != 1u)
            return DIC_STATUS_INVALID_ARGUMENT;
        for (pass = 0u; pass < pass_count; ++pass)
        {
            uint32_t length_value;
            size_t range_index;
            uint32_t length_bits = parser->terminated_passes
                ? codeblock->lblock
                : codeblock->lblock + dic_j2k_packet_floor_log2(pass_count);

            status = dic_j2k_packet_reader_read_bits(reader, length_bits, &length_value);
            if (status != DIC_STATUS_OK)
                return status;
            status = dic_j2k_packet_parse_result_push_range(
                result,
                subband_index,
                absolute_codeblock_index,
                codeblock->coding_passes + pass,
                length_value,
                &range_index
            );
            if (status != DIC_STATUS_OK)
                return status;
            status = dic_j2k_packet_pending_range_list_push(pending, range_index, length_value);
            if (status != DIC_STATUS_OK)
                return status;
            if (!parser->terminated_passes)
                break;
        }
        codeblock->coding_passes += pass_count;
    }
    return DIC_STATUS_OK;
}

dic_status dic_j2k_packet_header_parser_parse(
    dic_j2k_packet_header_parser *parser,
    const uint8_t *payload,
    size_t payload_size,
    uint16_t layer_index,
    dic_j2k_packet_parse_result *result
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_packet_bit_reader reader;
    dic_j2k_packet_pending_range_list pending = {0};
    size_t subband_index;
    size_t pending_index;
    size_t body_offset;
    uint32_t nonempty;
    dic_status status;

    if (parser == NULL || result == NULL || (payload == NULL && payload_size > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;
    dic_j2k_packet_parse_result_free(result);
    dic_j2k_packet_parse_result_init(result);
    dic_j2k_packet_bit_reader_init(&reader, payload, payload_size);
    status = dic_j2k_packet_reader_read_bit(&reader, &nonempty);
    if (status != DIC_STATUS_OK)
        return status;
    result->is_empty = nonempty == 0u;
    if (nonempty)
    {
        for (subband_index = 0u; subband_index < parser->subband_count && status == DIC_STATUS_OK; ++subband_index)
        {
            dic_j2k_packet_parse_subband_state *subband = parser->subbands + subband_index;
            int by;

            for (by = 0; by < subband->blocks_y && status == DIC_STATUS_OK; ++by)
            {
                int bx;

                for (bx = 0; bx < subband->blocks_x && status == DIC_STATUS_OK; ++bx)
                {
                    status = dic_j2k_packet_header_parser_parse_codeblock(
                        parser,
                        subband,
                        subband_index,
                        bx,
                        by,
                        layer_index,
                        &reader,
                        result,
                        &pending
                    );
                }
            }
        }
    }
    if (status == DIC_STATUS_OK)
    {
        result->packet_header_size = dic_j2k_packet_reader_aligned_offset(&reader);
        body_offset = result->packet_header_size;
        if (body_offset > payload_size)
            status = DIC_J2K_FORMAT_ERROR;
    }
    for (pending_index = 0u; status == DIC_STATUS_OK && pending_index < pending.count; ++pending_index)
    {
        dic_j2k_packet_pending_range *item = pending.items + pending_index;

        if (item->length > payload_size - body_offset)
        {
            status = DIC_J2K_FORMAT_ERROR;
            break;
        }
        result->ranges[item->range_index].byte_offset = body_offset;
        body_offset += item->length;
    }
    if (status == DIC_STATUS_OK)
        result->packet_body_size = body_offset - result->packet_header_size;
    dic_j2k_packet_pending_range_list_free(&pending);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.6 Table B.4, packet headers use variable codewords for the number of coding passes. */
static dic_status dic_j2k_packet_header_append_coding_passes(
    dic_j2k_packet_header *header,
    uint32_t coding_passes
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_status status;

    if (header == NULL || coding_passes == 0u || coding_passes > 164u)
        return DIC_STATUS_INVALID_ARGUMENT;

    if (coding_passes == 1u)
        return dic_j2k_packet_header_append_bit(header, 0u);
    if (coding_passes == 2u)
    {
        status = dic_j2k_packet_header_append_bit(header, 1u);
        if (status != DIC_STATUS_OK)
            return status;
        return dic_j2k_packet_header_append_bit(header, 0u);
    }
    if (coding_passes <= 5u)
    {
        status = dic_j2k_packet_header_append_bits(header, 0xcu | (coding_passes - 3u), 4u);
        return status;
    }
    if (coding_passes <= 36u)
    {
        status = dic_j2k_packet_header_append_bits(header, 0xfu, 4u);
        if (status != DIC_STATUS_OK)
            return status;
        return dic_j2k_packet_header_append_bits(header, coding_passes - 6u, 5u);
    }

    status = dic_j2k_packet_header_append_bits(header, 0x1ffu, 9u);
    if (status != DIC_STATUS_OK)
        return status;
    return dic_j2k_packet_header_append_bits(header, coding_passes - 37u, 7u);
}

static dic_status dic_j2k_packet_header_append_codeblock_body(
    dic_j2k_packet_header *header,
    const dic_j2k_packet_codeblock *codeblock
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t lblock_bits;
    dic_status status;

    if (codeblock->lblock == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_j2k_packet_header_append_coding_passes(header, codeblock->coding_passes);
    if (status != DIC_STATUS_OK)
        return status;

    for (lblock_bits = 0u; lblock_bits < codeblock->lblock_increment; ++lblock_bits)
    {
        status = dic_j2k_packet_header_append_bit(header, 1u);
        if (status != DIC_STATUS_OK)
            return status;
    }
    status = dic_j2k_packet_header_append_bit(header, 0u);
    if (status != DIC_STATUS_OK)
        return status;

    if (codeblock->segment_count > 0u)
    {
        uint32_t segment;

        lblock_bits = codeblock->lblock + codeblock->lblock_increment;
        for (segment = 0u; segment < codeblock->segment_count; ++segment)
        {
            if (codeblock->segment_lengths == NULL || codeblock->segment_lengths[segment] > UINT32_MAX)
                return DIC_STATUS_INVALID_ARGUMENT;
            if (lblock_bits > 31u)
                return DIC_STATUS_INVALID_ARGUMENT;
            status = dic_j2k_packet_header_append_bits(
                header,
                (uint32_t)codeblock->segment_lengths[segment],
                lblock_bits
            );
            if (status != DIC_STATUS_OK)
                return status;
        }
        return DIC_STATUS_OK;
    }

    lblock_bits = codeblock->lblock
        + codeblock->lblock_increment
        + dic_j2k_packet_floor_log2(codeblock->coding_passes);
    if (lblock_bits > 31u)
        return DIC_STATUS_INVALID_ARGUMENT;

    return dic_j2k_packet_header_append_bits(header, codeblock->codeword_length, lblock_bits);
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.7.2, Lblock is incremented once before signalling all terminated segment lengths. */
static dic_status dic_j2k_packet_calculate_segment_lblock_increment(
    uint32_t current_lblock,
    const size_t *segment_lengths,
    uint32_t segment_count,
    uint32_t *increment
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t segment;
    uint32_t required_lblock = current_lblock;

    if (increment == NULL || current_lblock == 0u || segment_lengths == NULL || segment_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (segment = 0u; segment < segment_count; ++segment)
    {
        uint32_t required_bits;

        if (segment_lengths[segment] > UINT32_MAX)
            return DIC_STATUS_INVALID_ARGUMENT;
        required_bits = dic_j2k_packet_bit_width((uint32_t)segment_lengths[segment]);
        if (required_bits > required_lblock)
            required_lblock = required_bits;
    }

    *increment = required_lblock > current_lblock ? required_lblock - current_lblock : 0u;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.7.1 Equation B-19, length bits are Lblock plus floor(log2(coding passes added)). */
dic_status dic_j2k_packet_calculate_lblock_increment(
    uint32_t current_lblock,
    uint32_t coding_passes,
    uint32_t codeword_length,
    uint32_t *increment
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t pass_bits;
    uint32_t required_bits;

    if (increment == NULL || current_lblock == 0u || coding_passes == 0u || coding_passes > 164u)
        return DIC_STATUS_INVALID_ARGUMENT;
    pass_bits = dic_j2k_packet_floor_log2(coding_passes);
    required_bits = dic_j2k_packet_bit_width(codeword_length);
    if (required_bits <= current_lblock + pass_bits)
    {
        *increment = 0u;
        return DIC_STATUS_OK;
    }
    *increment = required_bits - current_lblock - pass_bits;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.4-B.10.7, a first-in-layer code-block contributes inclusion, zero bit-plane, pass count, Lblock, and length fields. */
dic_status dic_j2k_packet_prepare_codeblock(
    dic_j2k_packet_codeblock *codeblock,
    uint32_t zero_bitplanes,
    uint32_t coding_passes,
    uint32_t codeword_length
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_status status;
    uint32_t increment;

    if (codeblock == NULL || coding_passes == 0u || coding_passes > 164u)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_j2k_packet_calculate_lblock_increment(
        3u,
        coding_passes,
        codeword_length,
        &increment
    );
    if (status != DIC_STATUS_OK)
        return status;

    codeblock->included = 1;
    codeblock->first_inclusion = 1;
    codeblock->zero_bitplanes = zero_bitplanes;
    codeblock->coding_passes = coding_passes;
    codeblock->lblock = 3u;
    codeblock->lblock_increment = increment;
    codeblock->codeword_length = codeword_length;
    codeblock->segment_lengths = NULL;
    codeblock->segment_count = 0u;
    return DIC_STATUS_OK;
}

static uint32_t dic_j2k_packet_layer_pass_boundary(
    uint32_t coding_passes,
    uint16_t layer_index,
    uint16_t layers
)
{
    DIC_J2K_DEBUG_ENTER();
    return (uint32_t)(((uint64_t)coding_passes * (uint64_t)layer_index) / (uint64_t)layers);
}

static int dic_j2k_packet_stream_has_rd_slopes(const dic_j2k_codeblock_stream *stream)
{
    DIC_J2K_DEBUG_ENTER();
    return stream != NULL
        && stream->pass_lengths != NULL
        && stream->pass_rd_slopes != NULL
        && stream->coding_passes > 0u;
}

static size_t dic_j2k_packet_rd_total_bytes(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t subband_index;
    size_t total = 0u;

    if (subbands == NULL)
        return 0u;
    for (subband_index = 0u; subband_index < subband_count; ++subband_index)
    {
        size_t index;

        if (subbands[subband_index].streams == NULL)
            continue;
        for (index = 0u; index < subbands[subband_index].stream_count; ++index)
            total += subbands[subband_index].streams[index].mq.byte_count;
    }
    return total;
}

static uint32_t dic_j2k_packet_rd_passes_at_slope(
    const dic_j2k_codeblock_stream *stream,
    double threshold
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t pass;

    if (!dic_j2k_packet_stream_has_rd_slopes(stream))
        return 0u;
    for (pass = 0u; pass < stream->coding_passes; ++pass)
    {
        if (stream->pass_rd_slopes[pass] < threshold)
            break;
    }
    return pass;
}

static size_t dic_j2k_packet_rd_selected_bytes(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count,
    double threshold
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t subband_index;
    size_t total = 0u;

    if (subbands == NULL)
        return 0u;
    for (subband_index = 0u; subband_index < subband_count; ++subband_index)
    {
        size_t index;

        if (subbands[subband_index].streams == NULL)
            continue;
        for (index = 0u; index < subbands[subband_index].stream_count; ++index)
        {
            const dic_j2k_codeblock_stream *stream = subbands[subband_index].streams + index;
            uint32_t pass_count = dic_j2k_packet_rd_passes_at_slope(stream, threshold);
            uint32_t pass;

            for (pass = 0u; pass < pass_count; ++pass)
                total += stream->pass_lengths[pass];
        }
    }
    return total;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.8, a layer boundary is formed by selecting code-block truncation points with an RD slope threshold. */
static uint32_t dic_j2k_packet_rd_layer_pass_boundary(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count,
    const dic_j2k_codeblock_stream *stream,
    uint16_t layer_index,
    uint16_t layers
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t total_bytes;
    size_t target_bytes;
    size_t subband_index;
    double max_slope = 0.0;
    double low = 0.0;
    double high;
    unsigned int iteration;

    if (stream == NULL || layers == 0u)
        return 0u;
    if (layer_index == 0u)
        return 0u;
    if (layer_index >= layers)
        return stream->coding_passes;
    if (!dic_j2k_packet_stream_has_rd_slopes(stream))
        return dic_j2k_packet_layer_pass_boundary(stream->coding_passes, layer_index, layers);

    total_bytes = dic_j2k_packet_rd_total_bytes(subbands, subband_count);
    if (total_bytes == 0u)
        return 0u;
    target_bytes = (size_t)(((uint64_t)total_bytes * (uint64_t)layer_index) / (uint64_t)layers);
    if (target_bytes == 0u)
        return 0u;

    for (subband_index = 0u; subband_index < subband_count; ++subband_index)
    {
        size_t index;

        if (subbands[subband_index].streams == NULL)
            continue;
        for (index = 0u; index < subbands[subband_index].stream_count; ++index)
        {
            const dic_j2k_codeblock_stream *candidate = subbands[subband_index].streams + index;
            uint32_t pass;

            if (!dic_j2k_packet_stream_has_rd_slopes(candidate))
                continue;
            for (pass = 0u; pass < candidate->coding_passes; ++pass)
            {
                if (candidate->pass_rd_slopes[pass] > max_slope)
                    max_slope = candidate->pass_rd_slopes[pass];
            }
        }
    }
    if (max_slope <= 0.0)
        return dic_j2k_packet_layer_pass_boundary(stream->coding_passes, layer_index, layers);

    high = max_slope + 1.0;
    for (iteration = 0u; iteration < 48u; ++iteration)
    {
        double mid = (low + high) * 0.5;
        size_t selected = dic_j2k_packet_rd_selected_bytes(subbands, subband_count, mid);

        if (selected > target_bytes)
            low = mid;
        else
            high = mid;
    }
    return dic_j2k_packet_rd_passes_at_slope(stream, high);
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.7, terminated coding-pass segment lengths define legal truncation byte offsets. */
static dic_status dic_j2k_packet_codeblock_pass_range(
    const dic_j2k_codeblock_stream *stream,
    uint32_t start_pass,
    uint32_t pass_count,
    size_t *byte_offset,
    size_t *byte_length
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t pass;

    if (stream == NULL || byte_offset == NULL || byte_length == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (start_pass > stream->coding_passes || pass_count > stream->coding_passes - start_pass)
        return DIC_STATUS_INVALID_ARGUMENT;

    *byte_offset = 0u;
    *byte_length = 0u;
    if (stream->pass_lengths == NULL)
    {
        if (start_pass != 0u || pass_count != stream->coding_passes)
            return DIC_STATUS_INVALID_ARGUMENT;
        *byte_length = stream->mq.byte_count;
        return DIC_STATUS_OK;
    }

    for (pass = 0u; pass < start_pass; ++pass)
        *byte_offset += stream->pass_lengths[pass];
    for (pass = 0u; pass < pass_count; ++pass)
        *byte_length += stream->pass_lengths[start_pass + pass];
    if (*byte_offset > stream->mq.byte_count || *byte_length > stream->mq.byte_count - *byte_offset)
        return DIC_STATUS_INVALID_ARGUMENT;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.7.1, Lblock persists for a code-block across layer contributions. */
static dic_status dic_j2k_packet_lblock_before_layer(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count,
    const dic_j2k_codeblock_stream *stream,
    uint16_t layer_index,
    uint16_t layers,
    uint32_t *lblock
)
{
    DIC_J2K_DEBUG_ENTER();
    uint16_t layer;
    uint32_t current_lblock = 3u;

    if (stream == NULL || lblock == NULL || layers == 0u || layer_index >= layers)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (layer = 0u; layer < layer_index; ++layer)
    {
        uint32_t start_pass = dic_j2k_packet_rd_layer_pass_boundary(subbands, subband_count, stream, layer, layers);
        uint32_t end_pass = dic_j2k_packet_rd_layer_pass_boundary(subbands, subband_count, stream, (uint16_t)(layer + 1u), layers);
        uint32_t pass_count = end_pass - start_pass;
        size_t byte_offset;
        size_t byte_length;
        uint32_t increment;
        dic_status status;

        if (pass_count == 0u)
            continue;
        status = dic_j2k_packet_codeblock_pass_range(stream, start_pass, pass_count, &byte_offset, &byte_length);
        if (status != DIC_STATUS_OK)
            return status;
        (void)byte_offset;
        if (stream->pass_lengths != NULL)
        {
            status = dic_j2k_packet_calculate_segment_lblock_increment(
                current_lblock,
                stream->pass_lengths + start_pass,
                pass_count,
                &increment
            );
        }
        else
        {
            status = dic_j2k_packet_calculate_lblock_increment(
                current_lblock,
                pass_count,
                (uint32_t)byte_length,
                &increment
            );
        }
        if (status != DIC_STATUS_OK)
            return status;
        current_lblock += increment;
    }

    *lblock = current_lblock;
    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.8, packet headers order inclusion, zero bit-planes, coding passes, Lblock, and codeword lengths. */
dic_status dic_j2k_packet_header_append_codeblock(
    dic_j2k_packet_header *header,
    const dic_j2k_packet_codeblock *codeblock
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_status status;

    if (header == NULL || codeblock == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (codeblock->coding_passes > 164u)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_j2k_packet_header_append_bit(header, codeblock->included ? 1u : 0u);
    if (status != DIC_STATUS_OK || !codeblock->included)
        return status;

    if (codeblock->first_inclusion)
    {
        status = dic_j2k_packet_header_append_unary_zeros_then_one(
            header,
            codeblock->zero_bitplanes
        );
        if (status != DIC_STATUS_OK)
            return status;
    }

    return dic_j2k_packet_header_append_codeblock_body(header, codeblock);
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10 and B.10.8, packet data consists of a packet header followed by codeword bytes. */
dic_status dic_j2k_packet_build_single_codeblock_payload(
    const dic_j2k_packet_codeblock *codeblock,
    const uint8_t *codeword,
    size_t codeword_size,
    dic_j2k_packet_header *payload
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_packet_codeblock_payload contribution;

    if (codeblock == NULL || payload == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (codeword_size > 0u && codeword == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    contribution.header = *codeblock;
    contribution.codeword = codeword;
    contribution.codeword_size = codeword_size;
    return dic_j2k_packet_build_codeblock_payload(&contribution, 1u, payload);
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.8, packet data is one header followed by included code-block contributions in scan order. */
dic_status dic_j2k_packet_build_codeblock_payload(
    const dic_j2k_packet_codeblock_payload *codeblocks,
    size_t codeblock_count,
    dic_j2k_packet_header *payload
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t index;
    int nonempty = 0;
    dic_status status;

    if (payload == NULL || (codeblocks == NULL && codeblock_count > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_j2k_packet_header_free(payload);
    dic_j2k_packet_header_init(payload);

    for (index = 0u; index < codeblock_count; ++index)
    {
        if (codeblocks[index].header.included)
        {
            nonempty = 1;
            break;
        }
    }

    status = dic_j2k_packet_header_append_bit(payload, nonempty ? 1u : 0u);
    if (status == DIC_STATUS_OK && nonempty)
    {
        for (index = 0u; index < codeblock_count; ++index)
        {
            if (codeblocks[index].codeword_size > 0u && codeblocks[index].codeword == NULL)
            {
                dic_j2k_packet_header_free(payload);
                return DIC_STATUS_INVALID_ARGUMENT;
            }
            if (codeblocks[index].header.included
                && codeblocks[index].header.codeword_length != codeblocks[index].codeword_size)
            {
                dic_j2k_packet_header_free(payload);
                return DIC_STATUS_INVALID_ARGUMENT;
            }
            status = dic_j2k_packet_header_append_codeblock(payload, &codeblocks[index].header);
            if (status != DIC_STATUS_OK)
                break;
        }
    }
    if (status == DIC_STATUS_OK)
        status = dic_j2k_packet_header_finish(payload);
    if (status != DIC_STATUS_OK)
    {
        dic_j2k_packet_header_free(payload);
        return status;
    }

    for (index = 0u; index < codeblock_count; ++index)
    {
        if (!codeblocks[index].header.included)
            continue;
        status = dic_j2k_packet_reserve(payload, codeblocks[index].codeword_size);
        if (status != DIC_STATUS_OK)
        {
            dic_j2k_packet_header_free(payload);
            return status;
        }
        if (codeblocks[index].codeword_size > 0u)
        {
            memcpy(payload->data + payload->size, codeblocks[index].codeword, codeblocks[index].codeword_size);
            payload->size += codeblocks[index].codeword_size;
        }
    }

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.5-B.10.8 and Annex D.3, EBCOT code-block streams provide zero bit-planes, pass count, and codeword bytes for packetization. */
dic_status dic_j2k_packet_build_ebcot_payload(
    const dic_j2k_codeblock_stream *streams,
    size_t stream_count,
    dic_j2k_packet_header *payload
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_packet_codeblock_payload *contributions;
    size_t index;
    dic_status status;

    if (payload == NULL || (streams == NULL && stream_count > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;

    contributions = (dic_j2k_packet_codeblock_payload *)calloc(
        stream_count == 0u ? 1u : stream_count,
        sizeof(contributions[0])
    );
    if (contributions == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    for (index = 0u; index < stream_count; ++index)
    {
        if (streams[index].mq.byte_count == 0u || streams[index].coding_passes == 0u)
        {
            contributions[index].header.included = 0;
            continue;
        }

        status = dic_j2k_packet_prepare_codeblock(
            &contributions[index].header,
            streams[index].zero_bitplanes,
            streams[index].coding_passes,
            (uint32_t)streams[index].mq.byte_count
        );
        if (status != DIC_STATUS_OK)
        {
            free(contributions);
            return status;
        }
        contributions[index].codeword = streams[index].mq.data;
        contributions[index].codeword_size = streams[index].mq.byte_count;
    }

    status = dic_j2k_packet_build_codeblock_payload(contributions, stream_count, payload);
    free(contributions);
    return status;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.4-B.10.8, first inclusion and missing most-significant bit-planes are signalled by one tag tree per precinct sub-band. */
dic_status dic_j2k_packet_build_tagged_ebcot_layer_payload_with_header_size(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count,
    uint16_t layer_index,
    uint16_t layers,
    dic_j2k_packet_header *payload,
    size_t *packet_header_size
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_packet_codeblock_payload **prepared = NULL;
    size_t subband_index;
    int nonempty = 0;
    dic_status status = DIC_STATUS_OK;

    if (payload == NULL || packet_header_size == NULL || layers == 0u || layer_index >= layers
        || (subbands == NULL && subband_count > 0u))
    {
        return DIC_STATUS_INVALID_ARGUMENT;
    }

    prepared = (dic_j2k_packet_codeblock_payload **)calloc(
        subband_count == 0u ? 1u : subband_count,
        sizeof(prepared[0])
    );
    if (prepared == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    for (subband_index = 0u; subband_index < subband_count; ++subband_index)
    {
        const dic_j2k_packet_subband_payload *subband = subbands + subband_index;
        int first_block_x;
        int first_block_y;
        int packet_blocks_x;
        int packet_blocks_y;
        int by;
        size_t packet_stream_count;

        status = dic_j2k_packet_payload_window(
            subband,
            &first_block_x,
            &first_block_y,
            &packet_blocks_x,
            &packet_blocks_y
        );
        if (status != DIC_STATUS_OK)
            break;
        if ((size_t)subband->blocks_x > (size_t)-1 / (size_t)subband->blocks_y
            || subband->stream_count != (size_t)subband->blocks_x * (size_t)subband->blocks_y
            || (subband->streams == NULL && subband->stream_count > 0u)
            || (size_t)packet_blocks_x > (size_t)-1 / (size_t)packet_blocks_y)
        {
            status = DIC_STATUS_INVALID_ARGUMENT;
            break;
        }
        (void)first_block_x;
        (void)first_block_y;
        packet_stream_count = (size_t)packet_blocks_x * (size_t)packet_blocks_y;

        prepared[subband_index] = (dic_j2k_packet_codeblock_payload *)calloc(
            packet_stream_count == 0u ? 1u : packet_stream_count,
            sizeof(prepared[subband_index][0])
        );
        if (prepared[subband_index] == NULL)
        {
            status = DIC_STATUS_MEMORY_ERROR;
            break;
        }

        for (by = 0; by < packet_blocks_y && status == DIC_STATUS_OK; ++by)
        {
            int bx;

            for (bx = 0; bx < packet_blocks_x; ++bx)
            {
                size_t index = (size_t)by * (size_t)packet_blocks_x + (size_t)bx;
                size_t stream_index = dic_j2k_packet_payload_stream_index(subband, bx, by);
                const dic_j2k_codeblock_stream *stream = subband->streams + stream_index;
                uint32_t start_pass = dic_j2k_packet_rd_layer_pass_boundary(subbands, subband_count, stream, layer_index, layers);
                uint32_t end_pass = dic_j2k_packet_rd_layer_pass_boundary(subbands, subband_count, stream, (uint16_t)(layer_index + 1u), layers);
                uint32_t pass_count = end_pass - start_pass;
                uint32_t current_lblock;
                uint32_t increment;
                size_t byte_offset;
                size_t byte_length;

                if (stream->coding_passes == 0u || pass_count == 0u)
                {
                    prepared[subband_index][index].header.included = 0;
                    prepared[subband_index][index].header.first_inclusion = start_pass == 0u ? 1 : 0;
                    continue;
                }
                status = dic_j2k_packet_codeblock_pass_range(
                    stream,
                    start_pass,
                    pass_count,
                    &byte_offset,
                    &byte_length
                );
                if (status != DIC_STATUS_OK)
                    break;
                if (byte_length > UINT32_MAX)
                {
                    status = DIC_STATUS_INVALID_ARGUMENT;
                    break;
                }
                status = dic_j2k_packet_lblock_before_layer(
                    subbands,
                    subband_count,
                    stream,
                    layer_index,
                    layers,
                    &current_lblock
                );
                if (status != DIC_STATUS_OK)
                    break;
                if (stream->pass_lengths != NULL)
                {
                    status = dic_j2k_packet_calculate_segment_lblock_increment(
                        current_lblock,
                        stream->pass_lengths + start_pass,
                        pass_count,
                        &increment
                    );
                }
                else
                {
                    status = dic_j2k_packet_calculate_lblock_increment(
                        current_lblock,
                        pass_count,
                        (uint32_t)byte_length,
                        &increment
                    );
                }
                if (status != DIC_STATUS_OK)
                    break;
                prepared[subband_index][index].header.included = 1;
                prepared[subband_index][index].header.first_inclusion = start_pass == 0u ? 1 : 0;
                prepared[subband_index][index].header.zero_bitplanes = stream->zero_bitplanes;
                prepared[subband_index][index].header.coding_passes = pass_count;
                prepared[subband_index][index].header.lblock = current_lblock;
                prepared[subband_index][index].header.lblock_increment = increment;
                prepared[subband_index][index].header.codeword_length = (uint32_t)byte_length;
                prepared[subband_index][index].header.segment_lengths = stream->pass_lengths == NULL
                    ? NULL
                    : stream->pass_lengths + start_pass;
                prepared[subband_index][index].header.segment_count = stream->pass_lengths == NULL ? 0u : pass_count;
                prepared[subband_index][index].codeword = stream->mq.data == NULL ? NULL : stream->mq.data + byte_offset;
                prepared[subband_index][index].codeword_size = byte_length;
                nonempty = 1;
            }
        }
        if (status != DIC_STATUS_OK)
            break;
    }

    if (status == DIC_STATUS_OK)
    {
        dic_j2k_packet_header_free(payload);
        dic_j2k_packet_header_init(payload);
        status = dic_j2k_packet_header_append_bit(payload, nonempty ? 1u : 0u);
    }

    if (status == DIC_STATUS_OK && nonempty)
    {
        for (subband_index = 0u; subband_index < subband_count && status == DIC_STATUS_OK; ++subband_index)
        {
            const dic_j2k_packet_subband_payload *subband = subbands + subband_index;
            dic_j2k_packet_tagtree inclusion_tree;
            dic_j2k_packet_tagtree zero_tree;
            int first_block_x;
            int first_block_y;
            int packet_blocks_x;
            int packet_blocks_y;
            int by;

            dic_j2k_packet_tagtree_init(&inclusion_tree);
            dic_j2k_packet_tagtree_init(&zero_tree);
            status = dic_j2k_packet_payload_window(
                subband,
                &first_block_x,
                &first_block_y,
                &packet_blocks_x,
                &packet_blocks_y
            );
            (void)first_block_x;
            (void)first_block_y;
            if (status == DIC_STATUS_OK)
                status = dic_j2k_packet_tagtree_alloc(&inclusion_tree, packet_blocks_x, packet_blocks_y);
            if (status == DIC_STATUS_OK)
                status = dic_j2k_packet_tagtree_alloc(&zero_tree, packet_blocks_x, packet_blocks_y);

            for (by = 0; by < packet_blocks_y && status == DIC_STATUS_OK; ++by)
            {
                int bx;

                for (bx = 0; bx < packet_blocks_x; ++bx)
                {
                    size_t index = (size_t)by * (size_t)packet_blocks_x + (size_t)bx;
                    const dic_j2k_packet_codeblock *header = &prepared[subband_index][index].header;

                    status = dic_j2k_packet_tagtree_set_leaf(
                        &inclusion_tree,
                        bx,
                        by,
                        header->first_inclusion && header->included ? 0u : 1u
                    );
                    if (status != DIC_STATUS_OK)
                        break;
                    status = dic_j2k_packet_tagtree_set_leaf(
                        &zero_tree,
                        bx,
                        by,
                        header->first_inclusion && header->included ? header->zero_bitplanes : 0u
                    );
                    if (status != DIC_STATUS_OK)
                        break;
                }
            }

            if (status == DIC_STATUS_OK)
            {
                dic_j2k_packet_tagtree_build_minima(&inclusion_tree);
                dic_j2k_packet_tagtree_build_minima(&zero_tree);
            }

            for (by = 0; by < packet_blocks_y && status == DIC_STATUS_OK; ++by)
            {
                int bx;

                for (bx = 0; bx < packet_blocks_x; ++bx)
                {
                    size_t index = (size_t)by * (size_t)packet_blocks_x + (size_t)bx;
                    const dic_j2k_packet_codeblock *header = &prepared[subband_index][index].header;

                    if (header->first_inclusion)
                    {
                        status = dic_j2k_packet_tagtree_encode_leaf(&inclusion_tree, bx, by, 0u, payload);
                        if (status != DIC_STATUS_OK || !header->included)
                            continue;
                        status = dic_j2k_packet_tagtree_encode_leaf(&zero_tree, bx, by, header->zero_bitplanes, payload);
                        if (status != DIC_STATUS_OK)
                            break;
                    }
                    else
                    {
                        status = dic_j2k_packet_header_append_bit(payload, header->included ? 1u : 0u);
                        if (status != DIC_STATUS_OK || !header->included)
                            continue;
                    }
                    status = dic_j2k_packet_header_append_codeblock_body(payload, header);
                    if (status != DIC_STATUS_OK)
                        break;
                }
            }

            dic_j2k_packet_tagtree_free(&zero_tree);
            dic_j2k_packet_tagtree_free(&inclusion_tree);
        }
    }

    if (status == DIC_STATUS_OK)
        status = dic_j2k_packet_header_finish(payload);

    if (status == DIC_STATUS_OK)
        *packet_header_size = payload->size;

    if (status == DIC_STATUS_OK && nonempty)
    {
        for (subband_index = 0u; subband_index < subband_count && status == DIC_STATUS_OK; ++subband_index)
        {
            const dic_j2k_packet_subband_payload *subband = subbands + subband_index;
            int first_block_x;
            int first_block_y;
            int packet_blocks_x;
            int packet_blocks_y;
            size_t packet_stream_count;
            size_t index;

            status = dic_j2k_packet_payload_window(
                subband,
                &first_block_x,
                &first_block_y,
                &packet_blocks_x,
                &packet_blocks_y
            );
            (void)first_block_x;
            (void)first_block_y;
            if (status != DIC_STATUS_OK)
                break;
            packet_stream_count = (size_t)packet_blocks_x * (size_t)packet_blocks_y;
            for (index = 0u; index < packet_stream_count; ++index)
            {
                if (!prepared[subband_index][index].header.included)
                    continue;
                status = dic_j2k_packet_reserve(payload, prepared[subband_index][index].codeword_size);
                if (status != DIC_STATUS_OK)
                    break;
                if (prepared[subband_index][index].codeword_size > 0u)
                {
                    memcpy(
                        payload->data + payload->size,
                        prepared[subband_index][index].codeword,
                        prepared[subband_index][index].codeword_size
                    );
                    payload->size += prepared[subband_index][index].codeword_size;
                }
            }
        }
    }

    if (status != DIC_STATUS_OK)
        dic_j2k_packet_header_free(payload);
    for (subband_index = 0u; subband_index < subband_count; ++subband_index)
        free(prepared[subband_index]);
    free(prepared);
    return status;
}

dic_status dic_j2k_packet_build_tagged_ebcot_payload_with_header_size(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count,
    dic_j2k_packet_header *payload,
    size_t *packet_header_size
)
{
    DIC_J2K_DEBUG_ENTER();
    return dic_j2k_packet_build_tagged_ebcot_layer_payload_with_header_size(
        subbands,
        subband_count,
        0u,
        1u,
        payload,
        packet_header_size
    );
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex B.10.1, callers that do not need an EPH insertion point receive the complete packet payload. */
dic_status dic_j2k_packet_build_tagged_ebcot_payload(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count,
    dic_j2k_packet_header *payload
)
{
    DIC_J2K_DEBUG_ENTER();
    size_t packet_header_size;

    return dic_j2k_packet_build_tagged_ebcot_payload_with_header_size(
        subbands,
        subband_count,
        payload,
        &packet_header_size
    );
}

/* Reference: paper/T-REC-T.800-200208.pdf, B.10.8 and Table A.16, LRCP order emits one packet for each layer, resolution, component, and precinct. */
dic_status dic_j2k_packet_build_empty_lrcp_payload(
    uint16_t components,
    uint8_t decomposition_levels,
    uint16_t layers,
    dic_j2k_packet_header *payload
)
{
    DIC_J2K_DEBUG_ENTER();
    uint32_t packet_count;
    uint32_t packet;
    dic_status status;

    if (payload == NULL || components == 0u || decomposition_levels > 32u)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (layers == 0u)
        layers = 1u;

    packet_count = (uint32_t)layers * (uint32_t)components * ((uint32_t)decomposition_levels + 1u);
    if (packet_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_j2k_packet_header_free(payload);
    dic_j2k_packet_header_init(payload);

    for (packet = 0u; packet < packet_count; ++packet)
    {
        status = dic_j2k_packet_header_append_bit(payload, 0u);
        if (status != DIC_STATUS_OK)
        {
            dic_j2k_packet_header_free(payload);
            return status;
        }
        status = dic_j2k_packet_header_finish(payload);
        if (status != DIC_STATUS_OK)
        {
            dic_j2k_packet_header_free(payload);
            return status;
        }
        payload->finished = 0;
    }

    payload->finished = 1;
    return DIC_STATUS_OK;
}

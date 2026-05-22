/**
 * @file dic_j2k_packet.c
 * @brief Builds JPEG 2000 packet headers and packet payloads according to T.800 Annex B.
 *
 * This implementation packs packet-header bits with Annex B byte-stuffing rules, encodes
 * inclusion and zero-bitplane tag-trees, computes Lblock increments, and appends code-block
 * contributions. It uses a constrained single-layer/single-precinct model for the local
 * encoder rather than the full progression-order machinery in Annex B.12.
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

    lblock_bits = 3u
        + codeblock->lblock_increment
        + dic_j2k_packet_floor_log2(codeblock->coding_passes);
    if (lblock_bits > 31u)
        return DIC_STATUS_INVALID_ARGUMENT;

    return dic_j2k_packet_header_append_bits(header, codeblock->codeword_length, lblock_bits);
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
    codeblock->lblock_increment = increment;
    codeblock->codeword_length = codeword_length;
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
dic_status dic_j2k_packet_build_tagged_ebcot_payload(
    const dic_j2k_packet_subband_payload *subbands,
    size_t subband_count,
    dic_j2k_packet_header *payload
)
{
    DIC_J2K_DEBUG_ENTER();
    dic_j2k_packet_codeblock_payload **prepared = NULL;
    size_t subband_index;
    int nonempty = 0;
    dic_status status = DIC_STATUS_OK;

    if (payload == NULL || (subbands == NULL && subband_count > 0u))
        return DIC_STATUS_INVALID_ARGUMENT;

    prepared = (dic_j2k_packet_codeblock_payload **)calloc(
        subband_count == 0u ? 1u : subband_count,
        sizeof(prepared[0])
    );
    if (prepared == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    for (subband_index = 0u; subband_index < subband_count; ++subband_index)
    {
        const dic_j2k_packet_subband_payload *subband = subbands + subband_index;
        size_t index;

        if (subband->blocks_x <= 0 || subband->blocks_y <= 0
            || (size_t)subband->blocks_x > (size_t)-1 / (size_t)subband->blocks_y
            || subband->stream_count != (size_t)subband->blocks_x * (size_t)subband->blocks_y
            || (subband->streams == NULL && subband->stream_count > 0u))
        {
            status = DIC_STATUS_INVALID_ARGUMENT;
            break;
        }

        prepared[subband_index] = (dic_j2k_packet_codeblock_payload *)calloc(
            subband->stream_count == 0u ? 1u : subband->stream_count,
            sizeof(prepared[subband_index][0])
        );
        if (prepared[subband_index] == NULL)
        {
            status = DIC_STATUS_MEMORY_ERROR;
            break;
        }

        for (index = 0u; index < subband->stream_count; ++index)
        {
            const dic_j2k_codeblock_stream *stream = subband->streams + index;

            if (stream->mq.byte_count == 0u || stream->coding_passes == 0u)
            {
                prepared[subband_index][index].header.included = 0;
                continue;
            }
            status = dic_j2k_packet_prepare_codeblock(
                &prepared[subband_index][index].header,
                stream->zero_bitplanes,
                stream->coding_passes,
                (uint32_t)stream->mq.byte_count
            );
            if (status != DIC_STATUS_OK)
                break;
            prepared[subband_index][index].codeword = stream->mq.data;
            prepared[subband_index][index].codeword_size = stream->mq.byte_count;
            nonempty = 1;
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
            int by;

            dic_j2k_packet_tagtree_init(&inclusion_tree);
            dic_j2k_packet_tagtree_init(&zero_tree);
            status = dic_j2k_packet_tagtree_alloc(&inclusion_tree, subband->blocks_x, subband->blocks_y);
            if (status == DIC_STATUS_OK)
                status = dic_j2k_packet_tagtree_alloc(&zero_tree, subband->blocks_x, subband->blocks_y);

            for (by = 0; by < subband->blocks_y && status == DIC_STATUS_OK; ++by)
            {
                int bx;

                for (bx = 0; bx < subband->blocks_x; ++bx)
                {
                    size_t index = (size_t)by * (size_t)subband->blocks_x + (size_t)bx;
                    const dic_j2k_packet_codeblock *header = &prepared[subband_index][index].header;

                    status = dic_j2k_packet_tagtree_set_leaf(&inclusion_tree, bx, by, header->included ? 0u : 1u);
                    if (status != DIC_STATUS_OK)
                        break;
                    status = dic_j2k_packet_tagtree_set_leaf(&zero_tree, bx, by, header->included ? header->zero_bitplanes : 0u);
                    if (status != DIC_STATUS_OK)
                        break;
                }
            }

            if (status == DIC_STATUS_OK)
            {
                dic_j2k_packet_tagtree_build_minima(&inclusion_tree);
                dic_j2k_packet_tagtree_build_minima(&zero_tree);
            }

            for (by = 0; by < subband->blocks_y && status == DIC_STATUS_OK; ++by)
            {
                int bx;

                for (bx = 0; bx < subband->blocks_x; ++bx)
                {
                    size_t index = (size_t)by * (size_t)subband->blocks_x + (size_t)bx;
                    const dic_j2k_packet_codeblock *header = &prepared[subband_index][index].header;

                    status = dic_j2k_packet_tagtree_encode_leaf(&inclusion_tree, bx, by, 0u, payload);
                    if (status != DIC_STATUS_OK || !header->included)
                        continue;
                    status = dic_j2k_packet_tagtree_encode_leaf(&zero_tree, bx, by, header->zero_bitplanes, payload);
                    if (status != DIC_STATUS_OK)
                        break;
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

    if (status == DIC_STATUS_OK && nonempty)
    {
        for (subband_index = 0u; subband_index < subband_count && status == DIC_STATUS_OK; ++subband_index)
        {
            const dic_j2k_packet_subband_payload *subband = subbands + subband_index;
            size_t index;

            for (index = 0u; index < subband->stream_count; ++index)
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

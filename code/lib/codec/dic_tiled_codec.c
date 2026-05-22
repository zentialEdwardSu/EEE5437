#include "codec/dic_tiled_codec.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec/dic_basic_file.h"
#include "wavelet/dic_dwt53.h"

static FILE *dic_tiled_open_file(const char *path, const char *mode)
{
    FILE *file = NULL;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, mode) != 0)
        return NULL;
    return file;
#else
    return fopen(path, mode);
#endif
}

static int dic_tiled_write_u32_le(FILE *file, uint32_t value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24) & 0xffu);
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes);
}

static int dic_tiled_read_u32_le(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];

    if (file == NULL || value == NULL)
        return 0;
    if (fread(bytes, 1u, sizeof(bytes), file) != sizeof(bytes))
        return 0;

    *value = (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
    return 1;
}

static int dic_tiled_min_int(int a, int b)
{
    return a < b ? a : b;
}

void dic_tiled_encoded_init(dic_tiled_encoded_image *encoded)
{
    if (encoded == NULL)
        return;

    encoded->width = 0;
    encoded->height = 0;
    encoded->channels = 0;
    encoded->requested_levels = 0;
    encoded->quant_step = 0;
    encoded->tile_width = 0;
    encoded->tile_height = 0;
    encoded->tile_count = 0;
    encoded->tiles = NULL;
}

void dic_tiled_encoded_free(dic_tiled_encoded_image *encoded)
{
    int i;

    if (encoded == NULL)
        return;

    if (encoded->tiles != NULL)
    {
        for (i = 0; i < encoded->tile_count; ++i)
            dic_basic_encoded_free(&encoded->tiles[i].encoded);
    }

    free(encoded->tiles);
    dic_tiled_encoded_init(encoded);
}

int dic_tiled_effective_levels(int width, int height, int requested_levels)
{
    int levels = requested_levels;

    if (width <= 0 || height <= 0 || requested_levels <= 0)
        return 0;

    while (levels > 0)
    {
        if (dic_dwt53_validate_levels(width, height, levels) == DIC_STATUS_OK)
            return levels;
        --levels;
    }

    return 0;
}

static dic_status dic_tiled_validate_params(
    int width,
    int height,
    int channels,
    int requested_levels,
    int quant_step,
    int tile_width,
    int tile_height
)
{
    if (width <= 0 || height <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;
    if (channels != 1 && channels != 3)
        return DIC_HW4_INVALID_CHANNELS;
    if (requested_levels <= 0)
        return DIC_HW4_INVALID_LEVELS;
    if (quant_step <= 0 || tile_width <= 0 || tile_height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    return DIC_STATUS_OK;
}

static void dic_tiled_copy_tile_from_image(
    const uint8_t *input,
    int image_width,
    int channels,
    int tile_x,
    int tile_y,
    int tile_width,
    int tile_height,
    uint8_t *tile_data
)
{
    int y;

    for (y = 0; y < tile_height; ++y)
    {
        const uint8_t *src = input
            + ((((size_t)(tile_y + y) * (size_t)image_width) + (size_t)tile_x) * (size_t)channels);
        uint8_t *dst = tile_data + ((size_t)y * (size_t)tile_width * (size_t)channels);

        memcpy(dst, src, (size_t)tile_width * (size_t)channels);
    }
}

static void dic_tiled_copy_tile_to_image(
    const dic_image_u8 *tile_image,
    int tile_x,
    int tile_y,
    int image_width,
    uint8_t *output
)
{
    int y;

    for (y = 0; y < tile_image->height; ++y)
    {
        const uint8_t *src = tile_image->data
            + ((size_t)y * (size_t)tile_image->width * (size_t)tile_image->channels);
        uint8_t *dst = output
            + ((((size_t)(tile_y + y) * (size_t)image_width) + (size_t)tile_x)
                * (size_t)tile_image->channels);

        memcpy(dst, src, (size_t)tile_image->width * (size_t)tile_image->channels);
    }
}

dic_status dic_tiled_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int requested_levels,
    int quant_step,
    int tile_width,
    int tile_height,
    dic_tiled_encoded_image *encoded
)
{
    int tile_cols;
    int tile_rows;
    int tile_index = 0;
    int ty;
    dic_status status;

    if (input == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_tiled_validate_params(
        width,
        height,
        channels,
        requested_levels,
        quant_step,
        tile_width,
        tile_height
    );
    if (status != DIC_STATUS_OK)
        return status;

    tile_cols = (width + tile_width - 1) / tile_width;
    tile_rows = (height + tile_height - 1) / tile_height;

    dic_tiled_encoded_free(encoded);
    encoded->tiles = (dic_tiled_tile *)calloc((size_t)tile_cols * (size_t)tile_rows, sizeof(encoded->tiles[0]));
    if (encoded->tiles == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    encoded->width = width;
    encoded->height = height;
    encoded->channels = channels;
    encoded->requested_levels = requested_levels;
    encoded->quant_step = quant_step;
    encoded->tile_width = tile_width;
    encoded->tile_height = tile_height;
    encoded->tile_count = tile_cols * tile_rows;

    for (ty = 0; ty < tile_rows; ++ty)
    {
        int tx;

        for (tx = 0; tx < tile_cols; ++tx)
        {
            int x0 = tx * tile_width;
            int y0 = ty * tile_height;
            int tw = dic_tiled_min_int(tile_width, width - x0);
            int th = dic_tiled_min_int(tile_height, height - y0);
            int levels = dic_tiled_effective_levels(tw, th, requested_levels);
            uint8_t *tile_data;
            dic_tiled_tile *tile = encoded->tiles + tile_index;

            if (levels <= 0)
            {
                dic_tiled_encoded_free(encoded);
                return DIC_HW4_INVALID_LEVELS;
            }

            tile_data = (uint8_t *)malloc((size_t)tw * (size_t)th * (size_t)channels);
            if (tile_data == NULL)
            {
                dic_tiled_encoded_free(encoded);
                return DIC_STATUS_MEMORY_ERROR;
            }

            dic_tiled_copy_tile_from_image(input, width, channels, x0, y0, tw, th, tile_data);

            tile->x = x0;
            tile->y = y0;
            tile->width = tw;
            tile->height = th;
            status = dic_basic_encode_image(
                tile_data,
                tw,
                th,
                channels,
                levels,
                quant_step,
                &tile->encoded
            );
            free(tile_data);
            if (status != DIC_STATUS_OK)
            {
                dic_tiled_encoded_free(encoded);
                return status;
            }

            ++tile_index;
        }
    }

    return DIC_STATUS_OK;
}

dic_status dic_tiled_decode_image(
    const dic_tiled_encoded_image *encoded,
    dic_image_u8 *decoded
)
{
    int i;
    dic_status status;

    if (encoded == NULL || decoded == NULL || encoded->tiles == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_image_u8_alloc(decoded, encoded->width, encoded->height, encoded->channels);
    if (status != DIC_STATUS_OK)
        return status;

    for (i = 0; i < encoded->tile_count; ++i)
    {
        const dic_tiled_tile *tile = encoded->tiles + i;
        dic_image_u8 tile_image = {0};

        status = dic_basic_decode_image(&tile->encoded, &tile_image);
        if (status != DIC_STATUS_OK)
        {
            dic_image_u8_free(decoded);
            return status;
        }

        if (tile_image.width != tile->width
            || tile_image.height != tile->height
            || tile_image.channels != encoded->channels
            || tile->x < 0
            || tile->y < 0
            || tile->x + tile->width > encoded->width
            || tile->y + tile->height > encoded->height)
        {
            dic_image_u8_free(&tile_image);
            dic_image_u8_free(decoded);
            return DIC_HW4_FORMAT_ERROR;
        }

        dic_tiled_copy_tile_to_image(&tile_image, tile->x, tile->y, encoded->width, decoded->data);
        dic_image_u8_free(&tile_image);
    }

    return DIC_STATUS_OK;
}

dic_status dic_tiled_write_file(
    const char *path,
    const dic_tiled_encoded_image *encoded
)
{
    FILE *file = NULL;
    dic_status status = DIC_STATUS_OK;
    int i;

    if (path == NULL || encoded == NULL || encoded->tiles == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (encoded->width <= 0 || encoded->height <= 0 || encoded->tile_count <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;

    file = dic_tiled_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (fwrite(DIC_TILED_FILE_MAGIC, 1u, 4u, file) != 4u
        || !dic_tiled_write_u32_le(file, DIC_TILED_FILE_VERSION)
        || !dic_tiled_write_u32_le(file, (uint32_t)encoded->width)
        || !dic_tiled_write_u32_le(file, (uint32_t)encoded->height)
        || !dic_tiled_write_u32_le(file, (uint32_t)encoded->channels)
        || !dic_tiled_write_u32_le(file, (uint32_t)encoded->requested_levels)
        || !dic_tiled_write_u32_le(file, (uint32_t)encoded->quant_step)
        || !dic_tiled_write_u32_le(file, (uint32_t)encoded->tile_width)
        || !dic_tiled_write_u32_le(file, (uint32_t)encoded->tile_height)
        || !dic_tiled_write_u32_le(file, (uint32_t)encoded->tile_count))
    {
        fclose(file);
        return DIC_STATUS_IO_ERROR;
    }

    for (i = 0; i < encoded->tile_count; ++i)
    {
        const dic_tiled_tile *tile = encoded->tiles + i;

        if (!dic_tiled_write_u32_le(file, (uint32_t)tile->x)
            || !dic_tiled_write_u32_le(file, (uint32_t)tile->y)
            || !dic_tiled_write_u32_le(file, (uint32_t)tile->width)
            || !dic_tiled_write_u32_le(file, (uint32_t)tile->height))
        {
            status = DIC_STATUS_IO_ERROR;
            break;
        }

        status = dic_basic_write_stream(file, &tile->encoded);
        if (status != DIC_STATUS_OK)
            break;
    }

    if (fclose(file) != 0 && status == DIC_STATUS_OK)
        status = DIC_STATUS_IO_ERROR;
    return status;
}

dic_status dic_tiled_read_file(
    const char *path,
    dic_tiled_encoded_image *encoded
)
{
    FILE *file = NULL;
    char magic[4];
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t requested_levels;
    uint32_t quant_step;
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t tile_count;
    dic_status status = DIC_STATUS_OK;
    int i;

    if (path == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_tiled_encoded_free(encoded);
    file = dic_tiled_open_file(path, "rb");
    if (file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;

    if (fread(magic, 1u, sizeof(magic), file) != sizeof(magic)
        || memcmp(magic, DIC_TILED_FILE_MAGIC, sizeof(magic)) != 0
        || !dic_tiled_read_u32_le(file, &version)
        || !dic_tiled_read_u32_le(file, &width)
        || !dic_tiled_read_u32_le(file, &height)
        || !dic_tiled_read_u32_le(file, &channels)
        || !dic_tiled_read_u32_le(file, &requested_levels)
        || !dic_tiled_read_u32_le(file, &quant_step)
        || !dic_tiled_read_u32_le(file, &tile_width)
        || !dic_tiled_read_u32_le(file, &tile_height)
        || !dic_tiled_read_u32_le(file, &tile_count))
    {
        fclose(file);
        return DIC_HW4_FORMAT_ERROR;
    }

    if (version != DIC_TILED_FILE_VERSION
        || width == 0u
        || height == 0u
        || (channels != 1u && channels != 3u)
        || requested_levels == 0u
        || quant_step == 0u
        || tile_width == 0u
        || tile_height == 0u
        || tile_count == 0u
        || tile_count > 1000000u)
    {
        fclose(file);
        return DIC_HW4_FORMAT_ERROR;
    }

    encoded->tiles = (dic_tiled_tile *)calloc((size_t)tile_count, sizeof(encoded->tiles[0]));
    if (encoded->tiles == NULL)
    {
        fclose(file);
        return DIC_STATUS_MEMORY_ERROR;
    }

    encoded->width = (int)width;
    encoded->height = (int)height;
    encoded->channels = (int)channels;
    encoded->requested_levels = (int)requested_levels;
    encoded->quant_step = (int)quant_step;
    encoded->tile_width = (int)tile_width;
    encoded->tile_height = (int)tile_height;
    encoded->tile_count = (int)tile_count;

    for (i = 0; i < encoded->tile_count; ++i)
    {
        uint32_t x;
        uint32_t y;
        uint32_t tw;
        uint32_t th;
        dic_tiled_tile *tile = encoded->tiles + i;

        if (!dic_tiled_read_u32_le(file, &x)
            || !dic_tiled_read_u32_le(file, &y)
            || !dic_tiled_read_u32_le(file, &tw)
            || !dic_tiled_read_u32_le(file, &th))
        {
            status = DIC_HW4_FORMAT_ERROR;
            break;
        }

        tile->x = (int)x;
        tile->y = (int)y;
        tile->width = (int)tw;
        tile->height = (int)th;
        if (tile->width <= 0
            || tile->height <= 0
            || tile->x < 0
            || tile->y < 0
            || tile->x + tile->width > encoded->width
            || tile->y + tile->height > encoded->height)
        {
            status = DIC_HW4_FORMAT_ERROR;
            break;
        }

        status = dic_basic_read_stream(file, &tile->encoded);
        if (status != DIC_STATUS_OK)
            break;
        if (tile->encoded.width != tile->width
            || tile->encoded.height != tile->height
            || tile->encoded.channels != encoded->channels
            || tile->encoded.quant_step != encoded->quant_step)
        {
            status = DIC_HW4_FORMAT_ERROR;
            break;
        }
    }

    if (status == DIC_STATUS_OK && fgetc(file) != EOF)
        status = DIC_HW4_FORMAT_ERROR;

    fclose(file);
    if (status != DIC_STATUS_OK)
        dic_tiled_encoded_free(encoded);
    return status;
}

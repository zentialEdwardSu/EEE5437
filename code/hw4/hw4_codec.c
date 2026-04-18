#include "hw4/hw4_codec.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "hw4/hw4_jpeg2000.h"
#include "errors/errors.h"

static void dic_hw4_zero_encoded(dic_hw4_encoded_image *encoded)
{
    if (encoded == NULL)
        return;

    encoded->version = DIC_HW4_VERSION;
    encoded->flags = 0u;
    encoded->width = 0;
    encoded->height = 0;
    encoded->channels = 0;
    encoded->levels = 0;
    encoded->quality = 0;
    encoded->coefficient_count = 0u;
    encoded->coefficients = NULL;
}

size_t dic_hw4_coefficient_count(int width, int height, int channels)
{
    size_t plane_size;

    if (width <= 0 || height <= 0 || (channels != 1 && channels != 3))
        return 0u;

    plane_size = (size_t)width * (size_t)height;
    return plane_size * (size_t)channels;
}

void dic_hw4_image_free(dic_hw4_image_u8 *image)
{
    if (image == NULL)
        return;

    free(image->data);
    image->data = NULL;
    image->width = 0;
    image->height = 0;
    image->channels = 0;
}

void dic_hw4_encoded_free(dic_hw4_encoded_image *encoded)
{
    if (encoded == NULL)
        return;

    free(encoded->coefficients);
    dic_hw4_zero_encoded(encoded);
}

dic_status dic_hw4_validate_codec_params(
    int width,
    int height,
    int channels,
    int levels,
    int quality
)
{
    int current_width = width;
    int current_height = height;
    int level = 0;

    if (width <= 0 || height <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;

    if (channels != 1 && channels != 3)
        return DIC_HW4_INVALID_CHANNELS;

    if (levels <= 0)
        return DIC_HW4_INVALID_LEVELS;

    if (quality < 1 || quality > 100)
        return DIC_HW4_INVALID_QUALITY;

    for (level = 0; level < levels; ++level)
    {
        if (current_width < 2 || current_height < 2)
            return DIC_HW4_INVALID_LEVELS;

        current_width = dic_hw4_low_band_size(current_width);
        current_height = dic_hw4_low_band_size(current_height);
    }

    return DIC_STATUS_OK;
}

static dic_status dic_hw4_prepare_encoded_output(
    dic_hw4_encoded_image *encoded,
    int width,
    int height,
    int channels,
    int levels,
    int quality
)
{
    size_t coefficient_count = dic_hw4_coefficient_count(width, height, channels);

    dic_hw4_zero_encoded(encoded);
    encoded->coefficients = (int32_t *)calloc(coefficient_count, sizeof(int32_t));
    if (encoded->coefficients == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    encoded->version = DIC_HW4_VERSION;
    encoded->flags = 0u;
    encoded->width = width;
    encoded->height = height;
    encoded->channels = channels;
    encoded->levels = levels;
    encoded->quality = quality;
    encoded->coefficient_count = coefficient_count;
    return DIC_STATUS_OK;
}

dic_status dic_hw4_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int levels,
    int quality,
    dic_hw4_encoded_image *encoded
)
{
    dic_status status;
    size_t plane_size;
    int channel = 0;
    int y = 0;
    int x = 0;

    if (input == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_hw4_validate_codec_params(width, height, channels, levels, quality);
    if (status != DIC_STATUS_OK)
        return status;

    plane_size = (size_t)width * (size_t)height;
    status = dic_hw4_prepare_encoded_output(encoded, width, height, channels, levels, quality);
    if (status != DIC_STATUS_OK)
        return status;

    for (channel = 0; channel < channels; ++channel)
    {
        int32_t *plane = encoded->coefficients + ((size_t)channel * plane_size);

        for (y = 0; y < height; ++y)
        {
            for (x = 0; x < width; ++x)
            {
                size_t pixel_index = (size_t)y * (size_t)width + (size_t)x;
                size_t sample_index = pixel_index * (size_t)channels + (size_t)channel;
                plane[pixel_index] = (int32_t)input[sample_index];
            }
        }

        status = dic_hw4_forward_transform_plane(plane, width, height, levels);
        if (status != DIC_STATUS_OK)
        {
            dic_hw4_encoded_free(encoded);
            return status;
        }

        status = dic_hw4_quantize_plane(plane, width, height, levels, quality);
        if (status != DIC_STATUS_OK)
        {
            dic_hw4_encoded_free(encoded);
            return status;
        }
    }

    return DIC_STATUS_OK;
}

dic_status dic_hw4_decode_image(
    const dic_hw4_encoded_image *encoded,
    dic_hw4_image_u8 *decoded
)
{
    dic_status status;
    size_t plane_size;
    size_t coefficient_count;
    int32_t *working = NULL;
    uint8_t *output = NULL;
    int channel = 0;
    int y = 0;
    int x = 0;

    if (encoded == NULL || decoded == NULL || encoded->coefficients == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_hw4_validate_codec_params(
        encoded->width,
        encoded->height,
        encoded->channels,
        encoded->levels,
        encoded->quality
    );
    if (status != DIC_STATUS_OK)
        return status;

    plane_size = (size_t)encoded->width * (size_t)encoded->height;
    coefficient_count = plane_size * (size_t)encoded->channels;
    if (encoded->coefficient_count != coefficient_count)
        return DIC_HW4_FORMAT_ERROR;

    working = (int32_t *)malloc(coefficient_count * sizeof(int32_t));
    if (working == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    memcpy(working, encoded->coefficients, coefficient_count * sizeof(int32_t));

    output = (uint8_t *)malloc(coefficient_count * sizeof(uint8_t));
    if (output == NULL)
    {
        free(working);
        return DIC_STATUS_MEMORY_ERROR;
    }

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        int32_t *plane = working + ((size_t)channel * plane_size);

        status = dic_hw4_dequantize_plane(
            plane,
            encoded->width,
            encoded->height,
            encoded->levels,
            encoded->quality
        );
        if (status != DIC_STATUS_OK)
        {
            free(working);
            free(output);
            return status;
        }

        status = dic_hw4_inverse_transform_plane(
            plane,
            encoded->width,
            encoded->height,
            encoded->levels
        );
        if (status != DIC_STATUS_OK)
        {
            free(working);
            free(output);
            return status;
        }

        for (y = 0; y < encoded->height; ++y)
        {
            for (x = 0; x < encoded->width; ++x)
            {
                size_t pixel_index = (size_t)y * (size_t)encoded->width + (size_t)x;
                size_t sample_index = pixel_index * (size_t)encoded->channels + (size_t)channel;
                int32_t value = plane[pixel_index];

                if (value < 0)
                    value = 0;
                if (value > 255)
                    value = 255;
                output[sample_index] = (uint8_t)value;
            }
        }
    }

    free(working);
    decoded->width = encoded->width;
    decoded->height = encoded->height;
    decoded->channels = encoded->channels;
    decoded->data = output;
    return DIC_STATUS_OK;
}

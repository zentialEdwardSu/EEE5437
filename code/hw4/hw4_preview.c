#include "hw4/hw4_preview.h"

#include <stdlib.h>

static uint8_t dic_hw4_map_coefficient_to_u8(int32_t value, int32_t max_abs)
{
    int64_t mapped;

    if (max_abs <= 0)
        return 128u;

    mapped = 128 + (((int64_t)value * 127) / (int64_t)max_abs);
    if (mapped < 0)
        mapped = 0;
    if (mapped > 255)
        mapped = 255;
    return (uint8_t)mapped;
}

dic_status dic_hw4_build_preview_image(
    const dic_hw4_encoded_image *encoded,
    dic_hw4_image_u8 *preview
)
{
    size_t plane_size;
    uint8_t *preview_data;
    int channel;
    int y;
    int x;

    if (encoded == NULL || preview == NULL || encoded->coefficients == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    plane_size = (size_t)encoded->width * (size_t)encoded->height;
    preview_data = (uint8_t *)malloc(plane_size * (size_t)encoded->channels);
    if (preview_data == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        const int32_t *plane = encoded->coefficients + ((size_t)channel * plane_size);
        int32_t max_abs = 0;

        for (y = 0; y < encoded->height; ++y)
        {
            for (x = 0; x < encoded->width; ++x)
            {
                int32_t value = plane[((size_t)y * (size_t)encoded->width) + (size_t)x];
                int32_t abs_value = value >= 0 ? value : -value;
                if (abs_value > max_abs)
                    max_abs = abs_value;
            }
        }

        for (y = 0; y < encoded->height; ++y)
        {
            for (x = 0; x < encoded->width; ++x)
            {
                size_t pixel_index = (size_t)y * (size_t)encoded->width + (size_t)x;
                size_t sample_index = pixel_index * (size_t)encoded->channels + (size_t)channel;
                preview_data[sample_index] = dic_hw4_map_coefficient_to_u8(
                    plane[pixel_index],
                    max_abs
                );
            }
        }
    }

    preview->width = encoded->width;
    preview->height = encoded->height;
    preview->channels = encoded->channels;
    preview->data = preview_data;
    return DIC_STATUS_OK;
}

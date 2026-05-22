#include "image_u8/image_u8.h"

#include <stddef.h>
#include <stdlib.h>

void dic_image_u8_init(dic_image_u8 *image)
{
    if (image == NULL)
        return;

    image->width = 0;
    image->height = 0;
    image->channels = 0;
    image->data = NULL;
}

void dic_image_u8_free(dic_image_u8 *image)
{
    if (image == NULL)
        return;

    free(image->data);
    dic_image_u8_init(image);
}

// Calculate the total number of samples in the image, which is width * height * channels. Returns 0 if any dimension is invalid.
size_t dic_image_u8_sample_count(int width, int height, int channels)
{
    if (width <= 0 || height <= 0 || (channels != 1 && channels != 3))
        return 0u;

    return (size_t)width * (size_t)height * (size_t)channels;
}

dic_status dic_image_u8_alloc(
    dic_image_u8 *image,
    int width,
    int height,
    int channels
)
{
    size_t sample_count;

    if (image == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (width <= 0 || height <= 0)
        return DIC_HW4_INVALID_DIMENSIONS;
    if (channels != 1 && channels != 3)
        return DIC_HW4_INVALID_CHANNELS;

    sample_count = dic_image_u8_sample_count(width, height, channels);
    if (sample_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    dic_image_u8_free(image);
    image->data = (uint8_t *)calloc(sample_count, sizeof(uint8_t));
    if (image->data == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    image->width = width;
    image->height = height;
    image->channels = channels;
    return DIC_STATUS_OK;
}

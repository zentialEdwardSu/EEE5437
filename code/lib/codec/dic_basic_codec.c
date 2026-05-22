#include "codec/dic_basic_codec.h"

#include <stdlib.h>
#include <string.h>

#include "codec/dic_predict.h"
#include "codec/dic_quant.h"
#include "codec/dic_subband.h"
#include "wavelet/dic_dwt53.h"

void dic_basic_encoded_init(dic_basic_encoded_image *encoded)
{
    if (encoded == NULL)
        return;

    encoded->width = 0;
    encoded->height = 0;
    encoded->channels = 0;
    encoded->levels = 0;
    encoded->quant_step = 0;
    encoded->channel_streams = NULL;
}

void dic_basic_encoded_free(dic_basic_encoded_image *encoded)
{
    int channel;

    if (encoded == NULL)
        return;

    if (encoded->channel_streams != NULL)
    {
        for (channel = 0; channel < encoded->channels; ++channel)
            free(encoded->channel_streams[channel].symbols);
    }
    free(encoded->channel_streams);
    dic_basic_encoded_init(encoded);
}

static dic_status dic_basic_validate_params(
    int width,
    int height,
    int channels,
    int levels,
    int quant_step
)
{
    dic_status status;

    if (channels != 1 && channels != 3)
        return DIC_HW4_INVALID_CHANNELS;
    if (quant_step <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_dwt53_validate_levels(width, height, levels);
    if (status != DIC_STATUS_OK)
        return status;

    return DIC_STATUS_OK;
}

static dic_status dic_basic_prepare_output(
    dic_basic_encoded_image *encoded,
    int width,
    int height,
    int channels,
    int levels,
    int quant_step
)
{
    dic_basic_encoded_free(encoded);

    encoded->channel_streams = (dic_basic_channel_stream *)calloc(
        (size_t)channels,
        sizeof(encoded->channel_streams[0])
    );
    if (encoded->channel_streams == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    encoded->width = width;
    encoded->height = height;
    encoded->channels = channels;
    encoded->levels = levels;
    encoded->quant_step = quant_step;
    return DIC_STATUS_OK;
}

static void dic_basic_copy_channel_to_plane(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int channel,
    int32_t *plane
)
{
    int y;

    for (y = 0; y < height; ++y)
    {
        int x;

        for (x = 0; x < width; ++x)
        {
            size_t pixel = ((size_t)y * (size_t)width) + (size_t)x;
            plane[pixel] = (int32_t)input[(pixel * (size_t)channels) + (size_t)channel];
        }
    }
}

static uint8_t dic_basic_clamp_u8(int32_t value)
{
    if (value < 0)
        return 0u;
    if (value > 255)
        return 255u;
    return (uint8_t)value;
}

static void dic_basic_copy_plane_to_channel(
    const int32_t *plane,
    int width,
    int height,
    int channels,
    int channel,
    uint8_t *output
)
{
    int y;

    for (y = 0; y < height; ++y)
    {
        int x;

        for (x = 0; x < width; ++x)
        {
            size_t pixel = ((size_t)y * (size_t)width) + (size_t)x;
            output[(pixel * (size_t)channels) + (size_t)channel] =
                dic_basic_clamp_u8(plane[pixel]);
        }
    }
}

dic_status dic_basic_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int levels,
    int quant_step,
    dic_basic_encoded_image *encoded
)
{
    size_t plane_count;
    int32_t *plane = NULL;
    dic_rect_i32 ll_rect;
    dic_status status;
    int channel;

    if (input == NULL || encoded == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_basic_validate_params(width, height, channels, levels, quant_step);
    if (status != DIC_STATUS_OK)
        return status;

    plane_count = (size_t)width * (size_t)height;
    plane = (int32_t *)malloc(plane_count * sizeof(plane[0]));
    if (plane == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    status = dic_basic_prepare_output(encoded, width, height, channels, levels, quant_step);
    if (status != DIC_STATUS_OK)
    {
        free(plane);
        return status;
    }

    status = dic_subband_lowest_ll_rect(width, height, levels, &ll_rect);
    if (status != DIC_STATUS_OK)
    {
        free(plane);
        dic_basic_encoded_free(encoded);
        return status;
    }

    for (channel = 0; channel < channels; ++channel)
    {
        dic_scan_symbol_buffer buffer;

        dic_scan_symbol_buffer_init(&buffer);
        dic_basic_copy_channel_to_plane(input, width, height, channels, channel, plane);

        status = dic_dwt53_forward_plane(plane, width, height, levels);
        if (status == DIC_STATUS_OK)
            status = dic_quant_scalar_i32(plane, plane_count, quant_step);
        if (status == DIC_STATUS_OK)
            status = dic_predict_ll_left(plane, width, ll_rect);
        if (status == DIC_STATUS_OK)
            status = dic_scan_encode_plane(plane, width, height, levels, &buffer);

        if (status != DIC_STATUS_OK)
        {
            dic_scan_symbol_buffer_free(&buffer);
            free(plane);
            dic_basic_encoded_free(encoded);
            return status;
        }

        encoded->channel_streams[channel].symbols = buffer.symbols;
        encoded->channel_streams[channel].symbol_count = buffer.count;
    }

    free(plane);
    return DIC_STATUS_OK;
}

dic_status dic_basic_decode_image(
    const dic_basic_encoded_image *encoded,
    dic_image_u8 *decoded
)
{
    size_t plane_count;
    int32_t *plane = NULL;
    dic_rect_i32 ll_rect;
    dic_status status;
    int channel;

    if (encoded == NULL || decoded == NULL || encoded->channel_streams == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    status = dic_basic_validate_params(
        encoded->width,
        encoded->height,
        encoded->channels,
        encoded->levels,
        encoded->quant_step
    );
    if (status != DIC_STATUS_OK)
        return status;

    plane_count = (size_t)encoded->width * (size_t)encoded->height;
    plane = (int32_t *)malloc(plane_count * sizeof(plane[0]));
    if (plane == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    status = dic_image_u8_alloc(decoded, encoded->width, encoded->height, encoded->channels);
    if (status != DIC_STATUS_OK)
    {
        free(plane);
        return status;
    }

    status = dic_subband_lowest_ll_rect(
        encoded->width,
        encoded->height,
        encoded->levels,
        &ll_rect
    );
    if (status != DIC_STATUS_OK)
    {
        dic_image_u8_free(decoded);
        free(plane);
        return status;
    }

    for (channel = 0; channel < encoded->channels; ++channel)
    {
        const dic_basic_channel_stream *stream = encoded->channel_streams + channel;

        status = dic_scan_decode_plane(
            stream->symbols,
            stream->symbol_count,
            encoded->width,
            encoded->height,
            encoded->levels,
            plane
        );
        if (status == DIC_STATUS_OK)
            status = dic_unpredict_ll_left(plane, encoded->width, ll_rect);
        if (status == DIC_STATUS_OK)
            status = dic_dequant_scalar_i32(plane, plane_count, encoded->quant_step);
        if (status == DIC_STATUS_OK)
            status = dic_dwt53_inverse_plane(plane, encoded->width, encoded->height, encoded->levels);

        if (status != DIC_STATUS_OK)
        {
            dic_image_u8_free(decoded);
            free(plane);
            return status;
        }

        dic_basic_copy_plane_to_channel(
            plane,
            encoded->width,
            encoded->height,
            encoded->channels,
            channel,
            decoded->data
        );
    }

    free(plane);
    return DIC_STATUS_OK;
}

size_t dic_basic_symbol_count(const dic_basic_encoded_image *encoded)
{
    size_t total = 0u;
    int channel;

    if (encoded == NULL || encoded->channel_streams == NULL)
        return 0u;

    for (channel = 0; channel < encoded->channels; ++channel)
        total += encoded->channel_streams[channel].symbol_count;

    return total;
}

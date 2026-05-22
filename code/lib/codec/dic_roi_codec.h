#pragma once

#include "codec/dic_bitplane.h"
#include "codec/dic_subband.h"
#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIC_ROI_FILE_MAGIC "DICR"
#define DIC_ROI_FILE_VERSION 1u
#define DIC_ROI_DEFAULT_SHIFT 8

typedef struct dic_roi_channel_stream
{
    dic_bitplane_stream bitplanes;
} dic_roi_channel_stream;

typedef struct dic_roi_encoded_image
{
    int width;
    int height;
    int channels;
    int levels;
    int quant_step;
    int roi_shift;
    dic_rect_i32 roi_rect;
    dic_roi_channel_stream *channel_streams;
} dic_roi_encoded_image;

void dic_roi_encoded_init(dic_roi_encoded_image *encoded);
void dic_roi_encoded_free(dic_roi_encoded_image *encoded);

dic_status dic_roi_detect_auto(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    dic_rect_i32 *roi_rect
);

dic_status dic_roi_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int levels,
    int quant_step,
    dic_roi_encoded_image *encoded
);

dic_status dic_roi_decode_image(
    const dic_roi_encoded_image *encoded,
    int decoded_bitplanes,
    dic_image_u8 *decoded
);

dic_status dic_roi_write_file(
    const char *path,
    const dic_roi_encoded_image *encoded
);

dic_status dic_roi_read_file(
    const char *path,
    dic_roi_encoded_image *encoded
);

#ifdef __cplusplus
}
#endif

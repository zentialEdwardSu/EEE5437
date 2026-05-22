#pragma once

#include "codec/dic_bitplane.h"
#include "image_u8/image_u8.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIC_SNR_FILE_MAGIC "DICS"
#define DIC_SNR_FILE_VERSION 1u

typedef struct dic_snr_channel_stream
{
    dic_bitplane_stream bitplanes;
} dic_snr_channel_stream;

typedef struct dic_snr_encoded_image
{
    int width;
    int height;
    int channels;
    int levels;
    int quant_step;
    dic_snr_channel_stream *channel_streams;
} dic_snr_encoded_image;

void dic_snr_encoded_init(dic_snr_encoded_image *encoded);
void dic_snr_encoded_free(dic_snr_encoded_image *encoded);

dic_status dic_snr_encode_image(
    const uint8_t *input,
    int width,
    int height,
    int channels,
    int levels,
    int quant_step,
    dic_snr_encoded_image *encoded
);

dic_status dic_snr_decode_image(
    const dic_snr_encoded_image *encoded,
    int decoded_bitplanes,
    dic_image_u8 *decoded
);

dic_status dic_snr_write_file(
    const char *path,
    const dic_snr_encoded_image *encoded
);

dic_status dic_snr_read_file(
    const char *path,
    dic_snr_encoded_image *encoded
);

size_t dic_snr_layer_bit_count(
    const dic_snr_encoded_image *encoded,
    int decoded_bitplanes
);

int dic_snr_max_bitplanes(const dic_snr_encoded_image *encoded);

#ifdef __cplusplus
}
#endif

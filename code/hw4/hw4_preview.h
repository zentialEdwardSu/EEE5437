#pragma once

#include "hw4/hw4_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

dic_status dic_hw4_build_preview_image(
    const dic_hw4_encoded_image *encoded,
    dic_hw4_image_u8 *preview
);

#ifdef __cplusplus
}
#endif

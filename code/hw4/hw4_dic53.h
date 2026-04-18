#pragma once

#include "hw4/hw4_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

dic_status dic_hw4_write_encoded_file(
    const char *path,
    const dic_hw4_encoded_image *encoded
);

dic_status dic_hw4_read_encoded_file(
    const char *path,
    dic_hw4_encoded_image *encoded
);

#ifdef __cplusplus
}
#endif

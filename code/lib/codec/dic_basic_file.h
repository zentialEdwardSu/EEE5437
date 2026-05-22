#pragma once

#include <stdio.h>

#include "codec/dic_basic_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIC_BASIC_FILE_MAGIC "DICW"
#define DIC_BASIC_FILE_VERSION 1u

dic_status dic_basic_write_file(
    const char *path,
    const dic_basic_encoded_image *encoded
);

dic_status dic_basic_read_file(
    const char *path,
    dic_basic_encoded_image *encoded
);

dic_status dic_basic_write_stream(
    FILE *file,
    const dic_basic_encoded_image *encoded
);

dic_status dic_basic_read_stream(
    FILE *file,
    dic_basic_encoded_image *encoded
);

#ifdef __cplusplus
}
#endif

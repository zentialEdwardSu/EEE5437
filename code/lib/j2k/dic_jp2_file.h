#pragma once

#include "j2k/dic_j2k_codestream.h"

#ifdef __cplusplus
extern "C" {
#endif

enum dic_jp2_box_type
{
    DIC_JP2_BOX_JP = 0x6a502020u,
    DIC_JP2_BOX_FTYP = 0x66747970u,
    DIC_JP2_BOX_JP2H = 0x6a703268u,
    DIC_JP2_BOX_IHDR = 0x69686472u,
    DIC_JP2_BOX_COLR = 0x636f6c72u,
    DIC_JP2_BOX_JP2C = 0x6a703263u
};

dic_status dic_jp2_write_minimal_file(
    const char *path,
    const dic_j2k_basic_params *params
);

dic_status dic_jp2_write_file_with_codestream_payload(
    const char *path,
    const dic_j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
);

#ifdef __cplusplus
}
#endif

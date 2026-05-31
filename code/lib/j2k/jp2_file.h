#pragma once

#include "j2k/j2k_codestream.h"

#ifdef __cplusplus
extern "C" {
#endif

enum jp2_box_type
{
    jp2_BOX_JP = 0x6a502020u,
    jp2_BOX_FTYP = 0x66747970u,
    jp2_BOX_JP2H = 0x6a703268u,
    jp2_BOX_IHDR = 0x69686472u,
    jp2_BOX_COLR = 0x636f6c72u,
    jp2_BOX_JP2C = 0x6a703263u
};

dic_status jp2_write_minimal_file(
    const char *path,
    const j2k_basic_params *params
);

dic_status jp2_write_file_with_codestream_payload(
    const char *path,
    const j2k_basic_params *params,
    const uint8_t *payload,
    size_t payload_size
);

dic_status jp2_write_file_with_codestream_tile_parts(
    const char *path,
    const j2k_basic_params *params,
    const j2k_tile_part_payload *tile_parts,
    size_t tile_part_count
);

#ifdef __cplusplus
}
#endif

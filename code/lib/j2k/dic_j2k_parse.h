#pragma once

#include <stddef.h>

#include "j2k/dic_j2k_codestream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dic_j2k_codestream_info
{
    dic_j2k_basic_params params; /**< Parsed SIZ/COD/RGN parameters from the codestream. */
    size_t tile_part_payload_bytes; /**< Number of compressed tile-part payload bytes after SOD. */
} dic_j2k_codestream_info;

dic_status dic_j2k_read_codestream_info(
    const char *path,
    dic_j2k_codestream_info *info
);

dic_status dic_jp2_read_codestream_info(
    const char *path,
    dic_j2k_codestream_info *info
);

#ifdef __cplusplus
}
#endif

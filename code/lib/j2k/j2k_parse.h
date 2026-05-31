#pragma once

#include <stddef.h>

#include "j2k/j2k_codestream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct j2k_codestream_info
{
    j2k_basic_params params; /**< Parsed SIZ/COD/RGN parameters from the codestream. */
    size_t tile_part_payload_bytes; /**< Number of compressed bytes after the most recent parsed SOD. */
    size_t total_tile_part_payload_bytes; /**< Sum of compressed tile-part payload bytes after all parsed SOD markers. */
    uint32_t tile_part_count; /**< Number of SOT/SOD tile-parts found in the codestream. */
    uint16_t last_tile_index; /**< Isot value from the most recent parsed SOT marker segment. */
} j2k_codestream_info;

dic_status j2k_read_codestream_info(
    const char *path,
    j2k_codestream_info *info
);

dic_status jp2_read_codestream_info(
    const char *path,
    j2k_codestream_info *info
);

#ifdef __cplusplus
}
#endif

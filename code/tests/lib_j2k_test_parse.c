#include <stdio.h>

#include "j2k/dic_j2k_codestream.h"
#include "j2k/dic_j2k_parse.h"
#include "j2k/dic_jp2_file.h"
#include "test_helpers.h"

/* Reference: paper/T-REC-T.800-200208.pdf, A.3-A.6 and Annex I.5.2.1, parser reads codestream metadata from J2K and JP2. */
int main(void)
{
    const char *j2k_path = "dic_parse_test.j2k";
    const char *jp2_path = "dic_parse_test.jp2";
    dic_j2k_basic_params params = {0};
    dic_j2k_codestream_info info;

    params.width = 37u;
    params.height = 29u;
    params.components = 3u;
    params.decomposition_levels = 4u;
    params.reversible = 1u;
    params.multiple_component_transform = 1u;
    params.layers = 2u;
    params.tile_width = 16u;
    params.tile_height = 16u;
    params.roi_shift = 8u;

    DIC_EXPECT(dic_j2k_write_empty_packet_codestream(j2k_path, &params) == DIC_STATUS_OK);
    DIC_EXPECT(dic_j2k_read_codestream_info(j2k_path, &info) == DIC_STATUS_OK);
    DIC_EXPECT(info.params.width == params.width);
    DIC_EXPECT(info.params.height == params.height);
    DIC_EXPECT(info.params.components == params.components);
    DIC_EXPECT(info.params.decomposition_levels == params.decomposition_levels);
    DIC_EXPECT(info.params.multiple_component_transform == 1u);
    DIC_EXPECT(info.params.layers == params.layers);
    DIC_EXPECT(info.params.tile_width == params.tile_width);
    DIC_EXPECT(info.params.tile_height == params.tile_height);
    DIC_EXPECT(info.params.roi_shift == params.roi_shift);
    DIC_EXPECT(info.tile_part_payload_bytes == (size_t)params.layers * params.components * (params.decomposition_levels + 1u));

    DIC_EXPECT(dic_jp2_write_minimal_file(jp2_path, &params) == DIC_STATUS_OK);
    DIC_EXPECT(dic_jp2_read_codestream_info(jp2_path, &info) == DIC_STATUS_OK);
    DIC_EXPECT(info.params.width == params.width);
    DIC_EXPECT(info.params.height == params.height);
    DIC_EXPECT(info.params.components == params.components);

    remove(j2k_path);
    remove(jp2_path);
    return 0;
}

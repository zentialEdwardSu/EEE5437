#include <stdio.h>

#include "j2k/dic_j2k_codestream.h"
#include "test_helpers.h"

static int dic_test_read_u16_be(FILE *file, unsigned int *value)
{
    int hi = fgetc(file);
    int lo = fgetc(file);

    if (hi == EOF || lo == EOF)
        return 0;

    *value = ((unsigned int)hi << 8) | (unsigned int)lo;
    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, A.4.2 Table A.5, Psot is a 32-bit tile-part length. */
static int dic_test_read_u32_be(FILE *file, unsigned int *value)
{
    int b0 = fgetc(file);
    int b1 = fgetc(file);
    int b2 = fgetc(file);
    int b3 = fgetc(file);

    if (b0 == EOF || b1 == EOF || b2 == EOF || b3 == EOF)
        return 0;

    *value = ((unsigned int)b0 << 24)
        | ((unsigned int)b1 << 16)
        | ((unsigned int)b2 << 8)
        | (unsigned int)b3;
    return 1;
}

int main(void)
{
    const char *path = "dic_minimal_test.j2k";
    const char *payload_path = "dic_payload_test.j2k";
    const char *ebcot_path = "dic_ebcot_payload_test.j2k";
    const unsigned char payload[] = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u};
    const int32_t coefficients[] = {
        0, 4, -1, 0,
        2, 0, -7, 3,
        0, 0, 1, -2,
        9, 0, 0, 0
    };
    dic_j2k_basic_params params = {0};
    dic_j2k_codeblock_stream stream;
    FILE *file = NULL;
    unsigned int marker;
    int saw_siz = 0;
    int saw_cod = 0;
    int saw_qcd = 0;
    int saw_sot = 0;
    int saw_sod = 0;

    dic_j2k_codeblock_stream_init(&stream);
    params.width = 64u;
    params.height = 48u;
    params.components = 1u;
    params.decomposition_levels = 5u;
    params.reversible = 1u;
    params.multiple_component_transform = 0u;

    DIC_EXPECT(dic_j2k_write_minimal_codestream(path, &params) == DIC_STATUS_OK);

#if defined(_MSC_VER)
    DIC_EXPECT(fopen_s(&file, path, "rb") == 0);
#else
    file = fopen(path, "rb");
    DIC_EXPECT(file != NULL);
#endif

    DIC_EXPECT(dic_test_read_u16_be(file, &marker));
    DIC_EXPECT(marker == DIC_J2K_MARKER_SOC);

    while (dic_test_read_u16_be(file, &marker))
    {
        unsigned int length;

        if (marker == DIC_J2K_MARKER_EOC)
            break;
        if (marker == DIC_J2K_MARKER_SIZ)
            saw_siz = 1;
        else if (marker == DIC_J2K_MARKER_COD)
        {
            int scod;
            unsigned int layers;
            int mct;
            int levels;
            int codeblock_width;
            int codeblock_height;
            int codeblock_style;
            int transform;

            saw_cod = 1;
            DIC_EXPECT(dic_test_read_u16_be(file, &length));
            DIC_EXPECT(length == 12u);
            scod = fgetc(file);
            DIC_EXPECT(scod == 0);
            DIC_EXPECT(fgetc(file) == 0);
            DIC_EXPECT(dic_test_read_u16_be(file, &layers));
            DIC_EXPECT(layers == 1u);
            mct = fgetc(file);
            levels = fgetc(file);
            codeblock_width = fgetc(file);
            codeblock_height = fgetc(file);
            codeblock_style = fgetc(file);
            transform = fgetc(file);
            DIC_EXPECT(mct == 0);
            DIC_EXPECT(levels == params.decomposition_levels);
            DIC_EXPECT(codeblock_width == 4);
            DIC_EXPECT(codeblock_height == 4);
            DIC_EXPECT(codeblock_style == 0);
            DIC_EXPECT(transform == 1);
            continue;
        }
        else if (marker == DIC_J2K_MARKER_QCD)
        {
            unsigned int expected_length = 4u + 3u * params.decomposition_levels;
            unsigned int i;
            int sqcd;
            static const int expected_high_steps[] = {9 << 3, 9 << 3, 10 << 3};

            saw_qcd = 1;
            DIC_EXPECT(dic_test_read_u16_be(file, &length));
            DIC_EXPECT(length == expected_length);
            sqcd = fgetc(file);
            DIC_EXPECT(sqcd == 0x40);
            DIC_EXPECT(fgetc(file) == (8 << 3));
            for (i = 0u; i < 3u * params.decomposition_levels; ++i)
                DIC_EXPECT(fgetc(file) == expected_high_steps[i % 3u]);
            continue;
        }
        else if (marker == DIC_J2K_MARKER_SOT)
            saw_sot = 1;
        else if (marker == DIC_J2K_MARKER_SOD)
        {
            saw_sod = 1;
            continue;
        }

        DIC_EXPECT(dic_test_read_u16_be(file, &length));
        DIC_EXPECT(fseek(file, (long)length - 2L, SEEK_CUR) == 0);
    }

    DIC_EXPECT(marker == DIC_J2K_MARKER_EOC);
    DIC_EXPECT(saw_siz);
    DIC_EXPECT(saw_cod);
    DIC_EXPECT(saw_qcd);
    DIC_EXPECT(saw_sot);
    DIC_EXPECT(saw_sod);

    fclose(file);
    remove(path);

    DIC_EXPECT(dic_j2k_write_codestream_with_payload(
        payload_path,
        &params,
        payload,
        sizeof(payload)
    ) == DIC_STATUS_OK);

#if defined(_MSC_VER)
    DIC_EXPECT(fopen_s(&file, payload_path, "rb") == 0);
#else
    file = fopen(payload_path, "rb");
    DIC_EXPECT(file != NULL);
#endif

    DIC_EXPECT(dic_test_read_u16_be(file, &marker));
    DIC_EXPECT(marker == DIC_J2K_MARKER_SOC);

    while (dic_test_read_u16_be(file, &marker))
    {
        unsigned int length;

        if (marker == DIC_J2K_MARKER_SOT)
            break;

        DIC_EXPECT(dic_test_read_u16_be(file, &length));
        DIC_EXPECT(fseek(file, (long)length - 2L, SEEK_CUR) == 0);
    }

    DIC_EXPECT(marker == DIC_J2K_MARKER_SOT);
    {
        unsigned int length;
        unsigned int tile_index;
        unsigned int tile_part_length;
        int tpsot;
        int tnsot;
        size_t i;

        DIC_EXPECT(dic_test_read_u16_be(file, &length));
        DIC_EXPECT(length == 10u);
        DIC_EXPECT(dic_test_read_u16_be(file, &tile_index));
        DIC_EXPECT(tile_index == 0u);
        DIC_EXPECT(dic_test_read_u32_be(file, &tile_part_length));
        DIC_EXPECT(tile_part_length == 14u + sizeof(payload));
        tpsot = fgetc(file);
        tnsot = fgetc(file);
        DIC_EXPECT(tpsot == 0);
        DIC_EXPECT(tnsot == 1);

        DIC_EXPECT(dic_test_read_u16_be(file, &marker));
        DIC_EXPECT(marker == DIC_J2K_MARKER_SOD);
        for (i = 0u; i < sizeof(payload); ++i)
            DIC_EXPECT(fgetc(file) == payload[i]);
        DIC_EXPECT(dic_test_read_u16_be(file, &marker));
        DIC_EXPECT(marker == DIC_J2K_MARKER_EOC);
    }

    fclose(file);
    remove(payload_path);

    DIC_EXPECT(dic_j2k_ebcot_encode_codeblock_rect(
        coefficients,
        4u,
        4u,
        DIC_J2K_SUBBAND_LL_LH,
        &stream
    ) == DIC_STATUS_OK);
    DIC_EXPECT(dic_j2k_write_ebcot_packet_codestream(
        ebcot_path,
        &params,
        &stream,
        1u
    ) == DIC_STATUS_OK);

#if defined(_MSC_VER)
    DIC_EXPECT(fopen_s(&file, ebcot_path, "rb") == 0);
#else
    file = fopen(ebcot_path, "rb");
    DIC_EXPECT(file != NULL);
#endif

    while (dic_test_read_u16_be(file, &marker))
    {
        unsigned int length;

        if (marker == DIC_J2K_MARKER_SOT)
            break;

        if (marker == DIC_J2K_MARKER_SOC)
            continue;
        DIC_EXPECT(dic_test_read_u16_be(file, &length));
        DIC_EXPECT(fseek(file, (long)length - 2L, SEEK_CUR) == 0);
    }
    DIC_EXPECT(marker == DIC_J2K_MARKER_SOT);
    {
        unsigned int length;
        unsigned int tile_index;
        unsigned int tile_part_length;

        DIC_EXPECT(dic_test_read_u16_be(file, &length));
        DIC_EXPECT(length == 10u);
        DIC_EXPECT(dic_test_read_u16_be(file, &tile_index));
        DIC_EXPECT(tile_index == 0u);
        DIC_EXPECT(dic_test_read_u32_be(file, &tile_part_length));
        DIC_EXPECT(tile_part_length > 14u + stream.mq.byte_count);
    }

    fclose(file);
    remove(ebcot_path);
    dic_j2k_codeblock_stream_free(&stream);
    return 0;
}

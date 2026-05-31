#include <stdio.h>

#include "j2k/j2k_codestream.h"
#include "j2k/jp2_file.h"
#include "test_helpers.h"

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.4, JP2 boxes start with length and type fields. */
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

/* Reference: paper/T-REC-T.800-200208.pdf, Annex A, codestream markers are 16-bit values. */
static int dic_test_read_u16_be(FILE *file, unsigned int *value)
{
    int hi = fgetc(file);
    int lo = fgetc(file);

    if (hi == EOF || lo == EOF)
        return 0;

    *value = ((unsigned int)hi << 8) | (unsigned int)lo;
    return 1;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex I.5, JP2 required top-level boxes. */
int main(void)
{
    const char *path = "dic_minimal_test.jp2";
    j2k_basic_params params = {0};
    FILE *file = NULL;
    unsigned int length;
    unsigned int type;
    unsigned int signature;
    unsigned int marker;

    params.width = 96u;
    params.height = 64u;
    params.components = 3u;
    params.decomposition_levels = 5u;
    params.reversible = 1u;
    params.multiple_component_transform = 1u;

    DIC_EXPECT(jp2_write_minimal_file(path, &params) == DIC_STATUS_OK);

#if defined(_MSC_VER)
    DIC_EXPECT(fopen_s(&file, path, "rb") == 0);
#else
    file = fopen(path, "rb");
    DIC_EXPECT(file != NULL);
#endif

    DIC_EXPECT(dic_test_read_u32_be(file, &length));
    DIC_EXPECT(dic_test_read_u32_be(file, &type));
    DIC_EXPECT(length == 12u);
    DIC_EXPECT(type == jp2_BOX_JP);
    DIC_EXPECT(dic_test_read_u32_be(file, &signature));
    DIC_EXPECT(signature == 0x0d0a870au);

    DIC_EXPECT(dic_test_read_u32_be(file, &length));
    DIC_EXPECT(dic_test_read_u32_be(file, &type));
    DIC_EXPECT(length == 20u);
    DIC_EXPECT(type == jp2_BOX_FTYP);
    DIC_EXPECT(fseek(file, 12L, SEEK_CUR) == 0);

    DIC_EXPECT(dic_test_read_u32_be(file, &length));
    DIC_EXPECT(dic_test_read_u32_be(file, &type));
    DIC_EXPECT(length == 45u);
    DIC_EXPECT(type == jp2_BOX_JP2H);
    DIC_EXPECT(fseek(file, 37L, SEEK_CUR) == 0);

    DIC_EXPECT(dic_test_read_u32_be(file, &length));
    DIC_EXPECT(dic_test_read_u32_be(file, &type));
    DIC_EXPECT(length == 0u);
    DIC_EXPECT(type == jp2_BOX_JP2C);

    DIC_EXPECT(dic_test_read_u16_be(file, &marker));
    DIC_EXPECT(marker == j2k_MARKER_SOC);

    fclose(file);
    remove(path);
    return 0;
}

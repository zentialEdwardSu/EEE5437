#include <stdio.h>
#include <stdlib.h>

#include "hw4/hw4_dic53.h"
#include "test_helpers.h"

static long dic_test_file_size(const char *path)
{
    FILE *file = NULL;
    long size = -1;

#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0)
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL)
        return -1;
    if (fseek(file, 0, SEEK_END) == 0)
        size = ftell(file);
    fclose(file);
    return size;
}

int main(void)
{
    const char *path = "hw4_test_entropy.dic53";
    const int width = 64;
    const int height = 64;
    const size_t coefficient_count = (size_t)width * (size_t)height;
    int32_t *source_values = (int32_t *)calloc(coefficient_count, sizeof(int32_t));
    dic_hw4_encoded_image written = {0};
    dic_hw4_encoded_image read_back = {0};
    long file_size;
    const long raw_dump_size = 33L + (long)(coefficient_count * sizeof(int32_t));
    size_t index;

    DIC_EXPECT(source_values != NULL);

    for (index = 0; index < coefficient_count; index += 257u)
        source_values[index] = (int32_t)((index % 17u) - 8);

    written.version = DIC_HW4_VERSION;
    written.flags = 0u;
    written.width = width;
    written.height = height;
    written.channels = 1;
    written.levels = 1;
    written.quality = 75;
    written.coefficient_count = coefficient_count;
    written.coefficients = source_values;

    DIC_EXPECT(dic_hw4_write_encoded_file(path, &written) == DIC_STATUS_OK);
    file_size = dic_test_file_size(path);
    DIC_EXPECT(file_size > 0);
    DIC_EXPECT(file_size < raw_dump_size);

    DIC_EXPECT(dic_hw4_read_encoded_file(path, &read_back) == DIC_STATUS_OK);
    DIC_EXPECT(read_back.coefficient_count == coefficient_count);

    for (index = 0; index < coefficient_count; ++index)
        DIC_EXPECT(read_back.coefficients[index] == source_values[index]);

    dic_hw4_encoded_free(&read_back);
    free(source_values);
    remove(path);
    return 0;
}

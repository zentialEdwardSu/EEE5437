#include <stdio.h>
#include <string.h>

#include "hw4/hw4_dic53.h"
#include "test_helpers.h"

int main(void)
{
    const char *path = "hw4_test_coefficients.dic53";
    const int32_t source_values[] = {12, -4, 0, 31, -17, 9};
    dic_hw4_encoded_image written = {0};
    dic_hw4_encoded_image read_back = {0};
    size_t index;

    written.version = DIC_HW4_VERSION;
    written.flags = 0u;
    written.width = 3;
    written.height = 2;
    written.channels = 1;
    written.levels = 1;
    written.quality = 75;
    written.coefficient_count = sizeof(source_values) / sizeof(source_values[0]);
    written.coefficients = (int32_t *)source_values;

    DIC_EXPECT(dic_hw4_write_encoded_file(path, &written) == DIC_STATUS_OK);
    DIC_EXPECT(dic_hw4_read_encoded_file(path, &read_back) == DIC_STATUS_OK);
    DIC_EXPECT(read_back.width == written.width);
    DIC_EXPECT(read_back.height == written.height);
    DIC_EXPECT(read_back.channels == written.channels);
    DIC_EXPECT(read_back.levels == written.levels);
    DIC_EXPECT(read_back.quality == written.quality);
    DIC_EXPECT(read_back.coefficient_count == written.coefficient_count);

    for (index = 0; index < read_back.coefficient_count; ++index)
        DIC_EXPECT(read_back.coefficients[index] == source_values[index]);

    dic_hw4_encoded_free(&read_back);
    remove(path);
    return 0;
}

#include <stdint.h>

#include "test_helpers.h"
#include "vec/vec.h"

typedef struct test_u16_vec {
    uint16_t* values;
    size_t count;
    size_t capacity;
} test_u16_vec;

DEFINE_VEC(test_u16_vec, test_u16_vec, uint16_t, values, 2u)

int main(void) {
    test_u16_vec vec;

    test_u16_vec_init(&vec);
    DIC_EXPECT(vec.values == NULL);
    DIC_EXPECT(vec.count == 0u);
    DIC_EXPECT(vec.capacity == 0u);

    DIC_EXPECT(test_u16_vec_append(&vec, 7u) == DIC_STATUS_OK);
    DIC_EXPECT(test_u16_vec_append(&vec, 11u) == DIC_STATUS_OK);
    DIC_EXPECT(test_u16_vec_append(&vec, 19u) == DIC_STATUS_OK);
    DIC_EXPECT(vec.count == 3u);
    DIC_EXPECT(vec.capacity >= 3u);
    DIC_EXPECT(vec.values[0] == 7u);
    DIC_EXPECT(vec.values[1] == 11u);
    DIC_EXPECT(vec.values[2] == 19u);

    DIC_EXPECT(test_u16_vec_reserve(&vec, 100u) == DIC_STATUS_OK);
    DIC_EXPECT(vec.capacity >= 100u);
    DIC_EXPECT(vec.count == 3u);
    DIC_EXPECT(vec.values[2] == 19u);

    test_u16_vec_free(&vec);
    DIC_EXPECT(vec.values == NULL);
    DIC_EXPECT(vec.count == 0u);
    DIC_EXPECT(vec.capacity == 0u);
    return 0;
}

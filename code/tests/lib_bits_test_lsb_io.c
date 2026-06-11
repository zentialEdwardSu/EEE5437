#include "bits/bits.h"
#include "test_helpers.h"

int main(void) {
    bits_lsb_writer writer;
    bits_lsb_reader reader;
    int pattern[10] = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1};
    int i;

    bits_lsb_writer_init(&writer);
    DIC_EXPECT(bits_lsb_writer_ensure_bits(&writer, 10u) == DIC_STATUS_OK);
    for (i = 0; i < 10; ++i) bits_lsb_write(&writer, pattern[i]);
    bits_lsb_writer_flush(&writer);

    DIC_EXPECT(writer.bit_count == 10u);
    DIC_EXPECT(writer.bytes[0] == 0x4du);
    DIC_EXPECT((writer.bytes[1] & 0x03u) == 0x03u);

    bits_lsb_reader_init(&reader, writer.bytes, writer.bit_count);
    for (i = 0; i < 10; ++i)
        DIC_EXPECT(bits_lsb_read(&reader) == pattern[i]);
    DIC_EXPECT(bits_lsb_read(&reader) == 0);
    DIC_EXPECT(reader.bits_read == 10u);

    bits_lsb_writer_free(&writer);
    DIC_EXPECT(writer.bytes == NULL);
    DIC_EXPECT(writer.bit_count == 0u);
    return 0;
}

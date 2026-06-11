#include "bits/bits.h"

#include <stdlib.h>
#include <string.h>

#include "vec/vec.h"

void bits_u32_to_le(uint32_t value, unsigned char bytes[4]) {
    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24) & 0xffu);
}

uint32_t bits_u32_from_le(const unsigned char bytes[4]) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

uint32_t bits_float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float bits_float_from_bits(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

int bits_write_u32(FILE* file, uint32_t value) {
    unsigned char bytes[4];
    bits_u32_to_le(value, bytes);
    return fwrite(bytes, 1u, 4u, file) == 4u;
}

int bits_read_u32(FILE* file, uint32_t* value) {
    unsigned char bytes[4];
    if (fread(bytes, 1u, 4u, file) != 4u) return 0;
    *value = bits_u32_from_le(bytes);
    return 1;
}

int bits_size_to_u32(size_t value, uint32_t* result) {
    if (value > UINT32_MAX) return 0;
    *result = (uint32_t)value;
    return 1;
}

void bits_lsb_writer_init(bits_lsb_writer* writer) {
    if (writer == NULL) return;
    writer->bytes = NULL;
    writer->byte_capacity = 0u;
    writer->bit_count = 0u;
    writer->current_byte = 0u;
    writer->bit_pos = 0;
}

void bits_lsb_writer_free(bits_lsb_writer* writer) {
    if (writer == NULL) return;
    free(writer->bytes);
    bits_lsb_writer_init(writer);
}

dic_status bits_lsb_writer_ensure_bits(bits_lsb_writer* writer,
                                       size_t total_bits) {
    size_t needed;
    if (writer == NULL) return DIC_STATUS_INVALID_ARGUMENT;
    needed = (total_bits + 7u) / 8u;
    return dic_vec_reserve_storage((void**)&writer->bytes,
                                   &writer->byte_capacity,
                                   sizeof(writer->bytes[0]), needed, 64u);
}

void bits_lsb_write(bits_lsb_writer* writer, int bit) {
    if (writer == NULL) return;
    writer->current_byte |= (unsigned char)((bit & 1) << writer->bit_pos);
    ++writer->bit_pos;
    ++writer->bit_count;
    if (writer->bit_pos == 8) {
        writer->bytes[writer->bit_count / 8u - 1u] = writer->current_byte;
        writer->current_byte = 0u;
        writer->bit_pos = 0;
    }
}

void bits_lsb_writer_flush(bits_lsb_writer* writer) {
    if (writer == NULL || writer->bit_pos == 0) return;
    writer->bytes[writer->bit_count / 8u] = writer->current_byte;
    writer->current_byte = 0u;
    writer->bit_pos = 0;
}

void bits_lsb_reader_init(bits_lsb_reader* reader, const unsigned char* bytes,
                          size_t bit_count) {
    if (reader == NULL) return;
    reader->bytes = bytes;
    reader->byte_count = (bit_count + 7u) / 8u;
    reader->bit_count = bit_count;
    reader->bits_read = 0u;
}

int bits_lsb_read(bits_lsb_reader* reader) {
    size_t byte_idx;
    size_t bit_idx;
    if (reader == NULL || reader->bits_read >= reader->bit_count) return 0;
    byte_idx = reader->bits_read / 8u;
    bit_idx = reader->bits_read % 8u;
    ++reader->bits_read;
    return (int)((reader->bytes[byte_idx] >> bit_idx) & 1u);
}

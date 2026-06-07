#include "bits/bits.h"
#include <string.h>
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
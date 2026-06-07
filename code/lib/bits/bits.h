#pragma once

#include <stdint.h>
#include <stdio.h>

void bits_u32_to_le(uint32_t value, unsigned char bytes[4]);
uint32_t bits_u32_from_le(const unsigned char bytes[4]);
uint32_t bits_float_bits(float value);
float bits_float_from_bits(uint32_t bits);
int bits_write_u32(FILE* file, uint32_t value);
int bits_read_u32(FILE* file, uint32_t* value);
int bits_size_to_u32(size_t value, uint32_t* result);

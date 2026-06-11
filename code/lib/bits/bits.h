#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "errors/errors.h"

typedef struct bits_lsb_writer {
    unsigned char* bytes;
    size_t byte_capacity;
    size_t bit_count;
    unsigned char current_byte;
    int bit_pos;
} bits_lsb_writer;

typedef struct bits_lsb_reader {
    const unsigned char* bytes;
    size_t byte_count;
    size_t bit_count;
    size_t bits_read;
} bits_lsb_reader;

void bits_u32_to_le(uint32_t value, unsigned char bytes[4]);
uint32_t bits_u32_from_le(const unsigned char bytes[4]);
uint32_t bits_float_bits(float value);
float bits_float_from_bits(uint32_t bits);
int bits_write_u32(FILE* file, uint32_t value);
int bits_read_u32(FILE* file, uint32_t* value);
int bits_size_to_u32(size_t value, uint32_t* result);

void bits_lsb_writer_init(bits_lsb_writer* writer);
void bits_lsb_writer_free(bits_lsb_writer* writer);
dic_status bits_lsb_writer_ensure_bits(bits_lsb_writer* writer,
                                       size_t total_bits);
void bits_lsb_write(bits_lsb_writer* writer, int bit);
void bits_lsb_writer_flush(bits_lsb_writer* writer);

void bits_lsb_reader_init(bits_lsb_reader* reader, const unsigned char* bytes,
                          size_t bit_count);
int bits_lsb_read(bits_lsb_reader* reader);

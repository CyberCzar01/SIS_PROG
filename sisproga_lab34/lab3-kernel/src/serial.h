#pragma once
#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_write(const char* s);
void serial_write_hex32(uint32_t v);
void serial_write_hex64(uint64_t v);

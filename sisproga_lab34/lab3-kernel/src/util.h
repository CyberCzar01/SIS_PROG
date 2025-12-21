#pragma once
#include <stdint.h>
#include <stddef.h>

size_t strnlen_s(const char* s, size_t maxn);
int memcmp_s(const void* a, const void* b, size_t n);
uint32_t checksum8(const void* p, size_t n);

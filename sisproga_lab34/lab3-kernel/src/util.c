#include "util.h"

size_t strnlen_s(const char* s, size_t maxn) {
  size_t i = 0;
  for (; i < maxn && s[i]; ++i) {}
  return i;
}

int memcmp_s(const void* a, const void* b, size_t n) {
  const uint8_t* x = (const uint8_t*)a;
  const uint8_t* y = (const uint8_t*)b;
  for (size_t i = 0; i < n; ++i) {
    if (x[i] != y[i]) return (int)x[i] - (int)y[i];
  }
  return 0;
}

uint32_t checksum8(const void* p, size_t n) {
  const uint8_t* x = (const uint8_t*)p;
  uint32_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += x[i];
  return sum & 0xFFu;
}

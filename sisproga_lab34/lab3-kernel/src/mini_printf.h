#pragma once
#include <stdint.h>

typedef void (*putc_cb_t)(char c, void* ctx);

void mini_printf(putc_cb_t cb, void* ctx, const char* fmt, ...);

void mini_vprintf(putc_cb_t cb, void* ctx, const char* fmt, void** va);

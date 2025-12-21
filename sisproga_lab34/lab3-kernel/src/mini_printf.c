#include "mini_printf.h"
#include <stdarg.h>
#include <stdint.h>

static void out_str(putc_cb_t cb, void* ctx, const char* s) {
  if (!s) s = "(null)";
  while (*s) cb(*s++, ctx);
}

static void out_hex32(putc_cb_t cb, void* ctx, uint32_t v) {
  static const char* H="0123456789ABCDEF";
  out_str(cb, ctx, "0x");
  for (int i=7;i>=0;--i) cb(H[(v>>(i*4))&0xF], ctx);
}

static void out_hex64(putc_cb_t cb, void* ctx, uint64_t v) {
  static const char* H="0123456789ABCDEF";
  out_str(cb, ctx, "0x");
  for (int i=15;i>=0;--i) cb(H[(v>>(i*4))&0xF], ctx);
}

static void out_u32(putc_cb_t cb, void* ctx, uint32_t v) {
  char buf[16];
  int i=0;
  if (v==0) { cb('0',ctx); return; }
  while (v && i<(int)sizeof(buf)) { buf[i++] = (char)('0'+(v%10)); v/=10; }
  while (i--) cb(buf[i], ctx);
}

void mini_printf(putc_cb_t cb, void* ctx, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  for (; *fmt; ++fmt) {
    if (*fmt != '%') { cb(*fmt, ctx); continue; }

    ++fmt;
    if (!*fmt) break;

    while (*fmt == '.' || (*fmt >= '0' && *fmt <= '9')) ++fmt;

    if (*fmt == '%') { cb('%', ctx); continue; }
    if (*fmt == 'c') { cb((char)va_arg(ap,int), ctx); continue; }
    if (*fmt == 's') { out_str(cb, ctx, va_arg(ap,const char*)); continue; }
    if (*fmt == 'u') { out_u32(cb, ctx, va_arg(ap,uint32_t)); continue; }
    if (*fmt == 'x' || *fmt == 'p') { out_hex32(cb, ctx, va_arg(ap,uint32_t)); continue; }
    if (*fmt == 'l') {
      ++fmt;
      if (*fmt=='x') { out_hex64(cb, ctx, va_arg(ap,uint64_t)); continue; }
    }

    cb('?', ctx);
  }

  va_end(ap);
}

void mini_vprintf(putc_cb_t cb, void* ctx, const char* fmt, void** va) {
  (void)va;
  out_str(cb, ctx, "[mini_vprintf DISABLED]\n");
}

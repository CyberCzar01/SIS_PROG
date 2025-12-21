#include "serial.h"

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

#define COM1 0x3F8

void serial_init(void) {
  outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x80);
  outb(COM1 + 0, 0x01);
  outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x03);
  outb(COM1 + 2, 0xC7);
  outb(COM1 + 4, 0x0B);
  (void)inb(COM1);
}

static int tx_ready(void) {
  return (inb(COM1 + 5) & 0x20) != 0;
}

void serial_putc(char c) {
  while (!tx_ready()) { }
  outb(COM1, (uint8_t)c);
}

void serial_write(const char* s) {
  while (*s) {
    if (*s == '\n') serial_putc('\r');
    serial_putc(*s++);
  }
}

static char hex_digit(uint8_t v) {
  return (v < 10) ? (char)('0' + v) : (char)('A' + (v - 10));
}

void serial_write_hex32(uint32_t v) {
  serial_write("0x");
  for (int i = 7; i >= 0; --i) {
    uint8_t n = (v >> (i * 4)) & 0xF;
    serial_putc(hex_digit(n));
  }
}
void serial_write_hex64(uint64_t v) {
  serial_write("0x");
  for (int i = 15; i >= 0; --i) {
    uint8_t n = (v >> (i * 4)) & 0xF;
    serial_putc(hex_digit(n));
  }
}

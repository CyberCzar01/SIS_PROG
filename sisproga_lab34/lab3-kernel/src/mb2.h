#pragma once
#include <stdint.h>

#define MB2_BOOTLOADER_MAGIC 0x36d76289u

#define MB2_TAG_END                0
#define MB2_TAG_CMDLINE            1
#define MB2_TAG_BOOT_LOADER_NAME   2
#define MB2_TAG_FRAMEBUFFER        8
#define MB2_TAG_ACPI_OLD           14
#define MB2_TAG_ACPI_NEW           15

typedef struct __attribute__((packed)) {
  uint32_t total_size;
  uint32_t reserved;
} mb2_info_t;

typedef struct __attribute__((packed)) {
  uint32_t type;
  uint32_t size;
} mb2_tag_t;

typedef struct __attribute__((packed)) {
  mb2_tag_t tag;
  uint64_t framebuffer_addr;
  uint32_t framebuffer_pitch;
  uint32_t framebuffer_width;
  uint32_t framebuffer_height;
  uint8_t  framebuffer_bpp;
  uint8_t  framebuffer_type;
  uint16_t reserved;

  uint8_t red_field_position;
  uint8_t red_mask_size;
  uint8_t green_field_position;
  uint8_t green_mask_size;
  uint8_t blue_field_position;
  uint8_t blue_mask_size;
} mb2_tag_framebuffer_t;

typedef struct __attribute__((packed)) {
  mb2_tag_t tag;
  uint8_t rsdp[];
} mb2_tag_acpi_t;

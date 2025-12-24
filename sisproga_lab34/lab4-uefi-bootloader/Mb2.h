#pragma once
#include <Uefi.h>

#define MB2_HEADER_MAGIC        0xE85250D6u
#define MB2_BOOTLOADER_MAGIC    0x36d76289u

#pragma pack(push,1)
typedef struct {
  UINT32 magic;
  UINT32 architecture;
  UINT32 header_length;
  UINT32 checksum;
} MB2_HEADER;

typedef struct {
  UINT16 type;
  UINT16 flags;
  UINT32 size;
} MB2_HEADER_TAG;

typedef struct {
  UINT32 total_size;
  UINT32 reserved;
} MB2_INFO;

typedef struct {
  UINT32 type;
  UINT32 size;
} MB2_TAG;

typedef struct {
  MB2_TAG tag;
  CHAR8 string[1];
} MB2_TAG_STRING;

typedef struct {
  MB2_TAG tag;
  UINT64 framebuffer_addr;
  UINT32 framebuffer_pitch;
  UINT32 framebuffer_width;
  UINT32 framebuffer_height;
  UINT8  framebuffer_bpp;
  UINT8  framebuffer_type;
  UINT16 reserved;
  UINT8  red_field_position;
  UINT8  red_mask_size;
  UINT8  green_field_position;
  UINT8  green_mask_size;
  UINT8  blue_field_position;
  UINT8  blue_mask_size;
} MB2_TAG_FRAMEBUFFER;
#pragma pack(pop)

static inline UINT32 MB2_ALIGN8(UINT32 x) { return (x + 7u) & ~7u; }

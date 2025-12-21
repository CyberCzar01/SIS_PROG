#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint8_t*  base;
  uint32_t  pitch;
  uint32_t  width;
  uint32_t  height;
  uint8_t   bpp;
  uint8_t   is_rgb;
  uint8_t   rpos, rsize, gpos, gsize, bpos, bsize;
} fb_t;

int fb_init_from_mb2(fb_t* fb,
                     uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h,
                     uint8_t bpp, uint8_t type,
                     uint8_t rpos, uint8_t rsz, uint8_t gpos, uint8_t gsz, uint8_t bpos, uint8_t bsz);

void fb_fill(fb_t* fb, uint32_t rgb);
void fb_rect(fb_t* fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb);

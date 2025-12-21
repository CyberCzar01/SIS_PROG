#include "fb.h"

static inline uint32_t pack_rgb(fb_t* fb, uint32_t rgb) {
  if (!fb->is_rgb || fb->bpp != 32) return rgb;
  uint32_t r = (rgb >> 16) & 0xFF;
  uint32_t g = (rgb >>  8) & 0xFF;
  uint32_t b = (rgb >>  0) & 0xFF;

  uint32_t v = 0;
  v |= (r & ((1u<<fb->rsize)-1)) << fb->rpos;
  v |= (g & ((1u<<fb->gsize)-1)) << fb->gpos;
  v |= (b & ((1u<<fb->bsize)-1)) << fb->bpos;
  return v;
}

int fb_init_from_mb2(fb_t* fb,
                     uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h,
                     uint8_t bpp, uint8_t type,
                     uint8_t rpos, uint8_t rsz, uint8_t gpos, uint8_t gsz, uint8_t bpos, uint8_t bsz) {
  fb->base = (uint8_t*)(uintptr_t)addr;
  fb->pitch = pitch;
  fb->width = w;
  fb->height = h;
  fb->bpp = bpp;
  fb->is_rgb = (type == 1);
  fb->rpos=rpos; fb->rsize=rsz;
  fb->gpos=gpos; fb->gsize=gsz;
  fb->bpos=bpos; fb->bsize=bsz;
  return (addr != 0 && w != 0 && h != 0 && pitch != 0);
}

void fb_fill(fb_t* fb, uint32_t rgb) {
  if (!fb->base) return;
  uint32_t color = pack_rgb(fb, rgb);
  for (uint32_t y = 0; y < fb->height; ++y) {
    uint32_t* row = (uint32_t*)(fb->base + y * fb->pitch);
    for (uint32_t x = 0; x < fb->width; ++x) row[x] = color;
  }
}

void fb_rect(fb_t* fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb) {
  if (!fb->base) return;
  uint32_t color = pack_rgb(fb, rgb);
  for (uint32_t yy = y; yy < y + h && yy < fb->height; ++yy) {
    uint32_t* row = (uint32_t*)(fb->base + yy * fb->pitch);
    for (uint32_t xx = x; xx < x + w && xx < fb->width; ++xx) row[xx] = color;
  }
}

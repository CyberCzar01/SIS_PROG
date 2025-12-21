#include <stdint.h>
#include <stddef.h>

#include "mb2.h"
#include "serial.h"
#include "fb.h"
#include "acpi.h"
#include "util.h"

static void s_write(const char* s) { serial_write(s); }

static void s_u32(uint32_t v) {
  char buf[16];
  int i = 0;
  if (v == 0) { serial_putc('0'); return; }
  while (v && i < (int)sizeof(buf)) {
    buf[i++] = (char)('0' + (v % 10));
    v /= 10;
  }
  while (i--) serial_putc(buf[i]);
}

static void s_hex32(uint32_t v) { serial_write_hex32(v); }
static void s_hex64(uint64_t v) { serial_write_hex64(v); }

static void s_nl(void) { serial_write("\n"); }

static void s_sig4(const char sig[4]) {
  for (int i = 0; i < 4; i++) {
    char c = sig[i];
    if (c < 32 || c > 126) c = '?';
    serial_putc(c);
  }
}

static void s_sig8(const char sig[8]) {
  for (int i = 0; i < 8; i++) {
    char c = sig[i];
    if (c < 32 || c > 126) c = '?';
    serial_putc(c);
  }
}

static int sig4_eq(const char sig[4], const char* lit) {
  return sig[0]==lit[0] && sig[1]==lit[1] && sig[2]==lit[2] && sig[3]==lit[3];
}

static fb_t   g_fb;
static int    g_fb_ok = 0;
static const rsdp_t* g_rsdp_copy_in_mb2 = 0;


static void k_acpi_dump_rsdp(const rsdp_t* rsdp) {
  s_write("[ACPI] RSDP copy @ "); s_hex32((uint32_t)(uintptr_t)rsdp); s_nl();
  s_write("[ACPI] signature: "); s_sig8(rsdp->signature); s_nl();
  s_write("[ACPI] revision: "); s_u32((uint32_t)rsdp->revision); s_nl();
  s_write("[ACPI] rsdt_address: "); s_hex32(rsdp->rsdt_address); s_nl();
  s_write("[ACPI] xsdt_address: "); s_hex64(rsdp->xsdt_address); s_nl();
}

static const madt_t* k_acpi_find_madt_via_rsdt(const rsdp_t* rsdp) {
  if (!rsdp || rsdp->rsdt_address == 0) {
    s_write("[ACPI][ERR] RSDT address is 0\n");
    return 0;
  }

  const acpi_sdt_header_t* rsdt = (const acpi_sdt_header_t*)(uintptr_t)rsdp->rsdt_address;

  s_write("[ACPI] RSDT @ "); s_hex32((uint32_t)(uintptr_t)rsdt);
  s_write(" sig="); s_sig4(rsdt->signature);
  s_write(" len="); s_u32(rsdt->length);
  s_nl();

  if (!sig4_eq(rsdt->signature, "RSDT")) {
    s_write("[ACPI][ERR] RSDT signature mismatch\n");
    return 0;
  }
  if (rsdt->length < sizeof(acpi_sdt_header_t)) {
    s_write("[ACPI][ERR] RSDT length too small\n");
    return 0;
  }

  uint32_t sum = checksum8(rsdt, rsdt->length);
  if (sum != 0) {
    s_write("[ACPI][WARN] RSDT checksum != 0, sum="); s_u32(sum); s_nl();
  }

  uint32_t entries_bytes = rsdt->length - (uint32_t)sizeof(acpi_sdt_header_t);
  uint32_t n = entries_bytes / 4;

  s_write("[ACPI] RSDT entries: "); s_u32(n); s_nl();

  const uint32_t* ent = (const uint32_t*)((const uint8_t*)rsdt + sizeof(acpi_sdt_header_t));
  for (uint32_t i = 0; i < n; i++) {
    uint32_t addr = ent[i];
    if (!addr) continue;

    const acpi_sdt_header_t* h = (const acpi_sdt_header_t*)(uintptr_t)addr;

    s_write("[ACPI]  ["); s_u32(i); s_write("] ");
    s_hex32(addr);
    s_write(" sig="); s_sig4(h->signature);
    s_write(" len="); s_u32(h->length);
    s_nl();

    if (sig4_eq(h->signature, "APIC")) {
      s_write("[ACPI]  -> Found MADT/APIC at "); s_hex32(addr); s_nl();
      return (const madt_t*)h;
    }
  }

  s_write("[ACPI][ERR] MADT/APIC not found via RSDT\n");
  return 0;
}

static void k_acpi_dump_madt(const madt_t* madt) {
  if (!madt) return;

  const acpi_sdt_header_t* h = &madt->hdr;

  s_write("[MADT] @ "); s_hex32((uint32_t)(uintptr_t)madt);
  s_write(" sig="); s_sig4(h->signature);
  s_write(" len="); s_u32(h->length);
  s_write(" rev="); s_u32(h->revision);
  s_nl();

  s_write("[MADT] local_apic_addr="); s_hex32(madt->local_apic_addr);
  s_write(" flags="); s_hex32(madt->flags);
  s_nl();

  if (h->length < sizeof(madt_t)) {
    s_write("[MADT][ERR] table too small\n");
    return;
  }

  uint32_t off = (uint32_t)sizeof(madt_t);
  uint32_t idx = 0;

  while (off + 2 <= h->length) {
    const uint8_t* e = ((const uint8_t*)madt) + off;
    uint8_t type = e[0];
    uint8_t len  = e[1];

    if (len < 2) {
      s_write("[MADT][ERR] entry len < 2 at off="); s_u32(off); s_nl();
      break;
    }
    if (off + len > h->length) {
      s_write("[MADT][ERR] entry overruns table at off="); s_u32(off);
      s_write(" len="); s_u32(len); s_nl();
      break;
    }

    s_write("[MADT] #"); s_u32(idx++);
    s_write(" off="); s_u32(off);
    s_write(" type="); s_u32(type);
    s_write(" len="); s_u32(len);
    s_write(" : ");

    if (type == 0 && len >= 8) {
      uint8_t acpi_proc_id = e[2];
      uint8_t apic_id      = e[3];
      uint32_t flags       = *(const uint32_t*)(e + 4);
      s_write("Local APIC: ACPI_ID="); s_u32(acpi_proc_id);
      s_write(" APIC_ID="); s_u32(apic_id);
      s_write(" flags="); s_hex32(flags);
      s_nl();
    } else if (type == 1 && len >= 12) {
      uint8_t ioapic_id = e[2];
      uint32_t ioapic_addr = *(const uint32_t*)(e + 4);
      uint32_t gsi_base = *(const uint32_t*)(e + 8);
      s_write("I/O APIC: id="); s_u32(ioapic_id);
      s_write(" addr="); s_hex32(ioapic_addr);
      s_write(" gsi_base="); s_u32(gsi_base);
      s_nl();
    } else if (type == 2 && len >= 10) {
      uint8_t bus = e[2];
      uint8_t src = e[3];
      uint32_t gsi = *(const uint32_t*)(e + 4);
      uint16_t flags = *(const uint16_t*)(e + 8);
      s_write("ISO: bus="); s_u32(bus);
      s_write(" src_irq="); s_u32(src);
      s_write(" gsi="); s_u32(gsi);
      s_write(" flags="); s_u32(flags);
      s_nl();
    } else if (type == 3 && len >= 8) {
      uint16_t flags = *(const uint16_t*)(e + 2);
      uint32_t gsi = *(const uint32_t*)(e + 4);
      s_write("NMI Source: flags="); s_u32(flags);
      s_write(" gsi="); s_u32(gsi);
      s_nl();
    } else if (type == 4 && len >= 6) {
      uint8_t acpi_id = e[2];
      uint16_t flags = *(const uint16_t*)(e + 3);
      uint8_t lint = e[5];
      s_write("Local APIC NMI: acpi_id="); s_u32(acpi_id);
      s_write(" flags="); s_u32(flags);
      s_write(" lint="); s_u32(lint);
      s_nl();
    } else if (type == 5 && len >= 12) {
      uint64_t lapic_addr = *(const uint64_t*)(e + 4);
      s_write("LAPIC Addr Override: lapic_addr="); s_hex64(lapic_addr);
      s_nl();
    } else {
      s_write("Unknown. Raw: ");
      for (uint32_t i = 0; i < len; i++) {
        uint8_t b = e[i];
        static const char* H="0123456789ABCDEF";
        serial_putc(H[b >> 4]);
        serial_putc(H[b & 0xF]);
        serial_putc(' ');
      }
      s_nl();
    }

    off += len;
  }

  s_write("[MADT] done\n");
}

static void parse_mb2(uint32_t mb_info_addr) {
  if ((mb_info_addr & 7u) != 0) {
    s_write("[MB2][WARN] mb_info is not 8-byte aligned: ");
    s_hex32(mb_info_addr);
    s_nl();
  }

  const mb2_info_t* info = (const mb2_info_t*)(uintptr_t)mb_info_addr;
  uint32_t total = info->total_size;

  s_write("[MB2] info @ "); s_hex32(mb_info_addr);
  s_write(" total_size="); s_u32(total);
  s_nl();

  if (total < sizeof(mb2_info_t) + 8) {
    s_write("[MB2][ERR] total_size too small\n");
    return;
  }
  if (total > (16u * 1024u * 1024u)) {
    s_write("[MB2][ERR] total_size too large (cap 16MiB). total="); s_u32(total); s_nl();
    return;
  }

  const uint8_t* p   = (const uint8_t*)info + sizeof(mb2_info_t);
  const uint8_t* end = (const uint8_t*)info + total;

  while (p + sizeof(mb2_tag_t) <= end) {
    const mb2_tag_t* tag = (const mb2_tag_t*)p;

    if (tag->size < 8) {
      s_write("[MB2][ERR] tag size < 8 at "); s_hex32((uint32_t)(uintptr_t)p); s_nl();
      break;
    }

    s_write("[MB2] tag type="); s_u32(tag->type);
    s_write(" size="); s_u32(tag->size);
    s_write(" @ "); s_hex32((uint32_t)(uintptr_t)p);
    s_nl();

    if (tag->type == MB2_TAG_END) {
      s_write("[MB2] END tag\n");
      break;
    }

    if (tag->type == MB2_TAG_FRAMEBUFFER && tag->size >= sizeof(mb2_tag_framebuffer_t)) {
      const mb2_tag_framebuffer_t* fb = (const mb2_tag_framebuffer_t*)tag;

      s_write("[MB2] framebuffer addr="); s_hex64(fb->framebuffer_addr);
      s_write(" "); s_u32(fb->framebuffer_width); s_write("x"); s_u32(fb->framebuffer_height);
      s_write(" pitch="); s_u32(fb->framebuffer_pitch);
      s_write(" bpp="); s_u32(fb->framebuffer_bpp);
      s_write(" type="); s_u32(fb->framebuffer_type);
      s_nl();

      g_fb_ok = fb_init_from_mb2(
        &g_fb,
        fb->framebuffer_addr, fb->framebuffer_pitch,
        fb->framebuffer_width, fb->framebuffer_height,
        fb->framebuffer_bpp, fb->framebuffer_type,
        fb->red_field_position, fb->red_mask_size,
        fb->green_field_position, fb->green_mask_size,
        fb->blue_field_position, fb->blue_mask_size
      );

      if (g_fb_ok) {
        fb_fill(&g_fb, 0x001030);                
        fb_rect(&g_fb, 20, 20, 360, 50, 0x00AA00);
      } else {
        s_write("[MB2][WARN] framebuffer init failed\n");
      }
    }

    if (tag->type == MB2_TAG_ACPI_OLD || tag->type == MB2_TAG_ACPI_NEW) {
      const mb2_tag_acpi_t* at = (const mb2_tag_acpi_t*)tag;
      const rsdp_t* rsdp = (const rsdp_t*)at->rsdp;
      g_rsdp_copy_in_mb2 = rsdp;

      s_write("[MB2] ACPI tag="); s_u32(tag->type);
      s_write(" rsdp_copy@"); s_hex32((uint32_t)(uintptr_t)rsdp);
      s_write(" rev="); s_u32((uint32_t)rsdp->revision);
      s_write(" sig="); s_sig8(rsdp->signature);
      s_nl();
    }

    uint32_t step = (tag->size + 7u) & ~7u;
    if (step == 0) {
      s_write("[MB2][ERR] step==0\n");
      break;
    }
    p += step;
  }
}

void kmain(uint32_t mb_magic, uint32_t mb_info_addr) {
  serial_init();

  s_write("\n=== LAB3 kernel start ===\n");

  s_write("[RAW] mb_magic="); s_hex32(mb_magic);
  s_write(" mb_info="); s_hex32(mb_info_addr);
  s_nl();

  if (mb_magic != MB2_BOOTLOADER_MAGIC) {
    s_write("[BOOT][ERR] wrong multiboot2 magic, expected ");
    s_hex32(MB2_BOOTLOADER_MAGIC);
    s_nl();
    for (;;) __asm__ volatile("hlt");
  }

  parse_mb2(mb_info_addr);

  if (!g_rsdp_copy_in_mb2) {
    s_write("[ACPI][ERR] no ACPI RSDP tag found (need tag 14 or 15)\n");
    for (;;) __asm__ volatile("hlt");
  }

  k_acpi_dump_rsdp(g_rsdp_copy_in_mb2);

  const madt_t* madt = k_acpi_find_madt_via_rsdt(g_rsdp_copy_in_mb2);
  if (!madt) {
    s_write("[ACPI][ERR] MADT/APIC not found\n");
    for (;;) __asm__ volatile("hlt");
  }

  k_acpi_dump_madt(madt);

  s_write("=== LAB3 done, halting ===\n");
  for (;;) __asm__ volatile("hlt");
}

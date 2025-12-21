#include "acpi.h"
#include "serial.h"
#include "mini_printf.h"
#include "util.h"

static void s_putc(char c, void* ctx) { (void)ctx; serial_putc(c); }

static void logf(const char* fmt, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, fmt);
  void** va = (void**)&ap;
  mini_vprintf(s_putc, 0, fmt, va);
  __builtin_va_end(ap);
}

static int sig4(const char sig[4], const char* lit) {
  return sig[0]==lit[0] && sig[1]==lit[1] && sig[2]==lit[2] && sig[3]==lit[3];
}

void acpi_dump_rsdp(const rsdp_t* rsdp) {
  logf("[ACPI] RSDP @ %p\n", (uint32_t)(uintptr_t)rsdp);
  logf("[ACPI] signature: %.8s\n", (const char*)rsdp->signature);
  logf("[ACPI] revision: %u\n", (uint32_t)rsdp->revision);
  logf("[ACPI] rsdt_address: %x\n", rsdp->rsdt_address);
  logf("[ACPI] xsdt_address: %lx\n", rsdp->xsdt_address);
}

static const acpi_sdt_header_t* rsdt_from_rsdp(const rsdp_t* rsdp) {
  if (rsdp->rsdt_address == 0) return 0;
  return (const acpi_sdt_header_t*)(uintptr_t)rsdp->rsdt_address;
}

const madt_t* acpi_find_madt_via_rsdt(const rsdp_t* rsdp) {
  const acpi_sdt_header_t* rsdt = rsdt_from_rsdp(rsdp);
  if (!rsdt) {
    logf("[ACPI][ERR] RSDT address is 0\n");
    return 0;
  }

  logf("[ACPI] RSDT @ %p sig=%.4s len=%u\n",
       (uint32_t)(uintptr_t)rsdt, rsdt->signature, rsdt->length);

  if (!sig4(rsdt->signature, "RSDT")) {
    logf("[ACPI][ERR] RSDT signature mismatch\n");
    return 0;
  }

  if ((rsdt->length < sizeof(acpi_sdt_header_t))) {
    logf("[ACPI][ERR] RSDT length too small\n");
    return 0;
  }

  uint32_t sum = checksum8(rsdt, rsdt->length);
  if (sum != 0) logf("[ACPI][WARN] RSDT checksum != 0 (sum=%u)\n", sum);

  uint32_t entries_bytes = rsdt->length - (uint32_t)sizeof(acpi_sdt_header_t);
  uint32_t n = entries_bytes / 4;
  const uint32_t* ent = (const uint32_t*)((const uint8_t*)rsdt + sizeof(acpi_sdt_header_t));

  logf("[ACPI] RSDT entries: %u\n", n);

  for (uint32_t i = 0; i < n; ++i) {
    uint32_t addr = ent[i];
    const acpi_sdt_header_t* h = (const acpi_sdt_header_t*)(uintptr_t)addr;
    if (!h) continue;

    logf("[ACPI]  [%u] %x sig=%.4s len=%u\n", i, addr, h->signature, h->length);

    if (sig4(h->signature, "APIC")) {
      logf("[ACPI]  -> Found MADT/APIC at %x\n", addr);
      return (const madt_t*)h;
    }
  }

  logf("[ACPI][ERR] MADT/APIC not found via RSDT\n");
  return 0;
}

static void dump_bytes(const void* p, uint32_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (uint32_t i=0;i<n;i++) {
    static const char* H="0123456789ABCDEF";
    serial_putc(H[b[i]>>4]);
    serial_putc(H[b[i]&0xF]);
    serial_putc(' ');
  }
  serial_putc('\n');
}

void acpi_dump_madt(const madt_t* madt) {
  if (!madt) return;

  const acpi_sdt_header_t* h = &madt->hdr;
  logf("[MADT] @ %p sig=%.4s len=%u rev=%u\n",
       (uint32_t)(uintptr_t)madt, h->signature, h->length, (uint32_t)h->revision);
  logf("[MADT] local_apic_addr=%x flags=%x\n", madt->local_apic_addr, madt->flags);

  uint32_t off = (uint32_t)sizeof(madt_t);
  while (off + 2 <= h->length) {
    const uint8_t* e = ((const uint8_t*)madt) + off;
    uint8_t type = e[0];
    uint8_t len  = e[1];
    if (len < 2) {
      logf("[MADT][ERR] entry len < 2 at off=%u\n", off);
      break;
    }
    if (off + len > h->length) {
      logf("[MADT][ERR] entry overruns table at off=%u len=%u\n", off, (uint32_t)len);
      break;
    }

    logf("[MADT] entry off=%u type=%u len=%u : ", off, (uint32_t)type, (uint32_t)len);

    if (type == 0 && len >= 8) {
      uint8_t acpi_proc_id = e[2];
      uint8_t apic_id      = e[3];
      uint32_t flags       = *(const uint32_t*)(e+4);
      logf("Local APIC: ACPI_ID=%u APIC_ID=%u flags=%x\n",
           (uint32_t)acpi_proc_id, (uint32_t)apic_id, flags);
    } else if (type == 1 && len >= 12) {
      uint8_t ioapic_id = e[2];
      uint32_t ioapic_addr = *(const uint32_t*)(e+4);
      uint32_t gsi_base = *(const uint32_t*)(e+8);
      logf("I/O APIC: id=%u addr=%x gsi_base=%u\n",
           (uint32_t)ioapic_id, ioapic_addr, gsi_base);
    } else if (type == 2 && len >= 10) {
      uint8_t bus = e[2];
      uint8_t src = e[3];
      uint32_t gsi = *(const uint32_t*)(e+4);
      uint16_t flags = *(const uint16_t*)(e+8);
      logf("ISO: bus=%u src_irq=%u gsi=%u flags=%u\n",
           (uint32_t)bus, (uint32_t)src, gsi, (uint32_t)flags);
    } else if (type == 3 && len >= 8) {
      uint16_t flags = *(const uint16_t*)(e+2);
      uint32_t gsi = *(const uint32_t*)(e+4);
      logf("NMI Source: flags=%u gsi=%u\n", (uint32_t)flags, gsi);
    } else if (type == 4 && len >= 6) {
      uint8_t acpi_id = e[2];
      uint16_t flags = *(const uint16_t*)(e+3);
      uint8_t lint = e[5];
      logf("Local APIC NMI: acpi_id=%u flags=%u lint=%u\n",
           (uint32_t)acpi_id, (uint32_t)flags, (uint32_t)lint);
    } else if (type == 5 && len >= 12) {
      uint64_t lapic_addr = *(const uint64_t*)(e+4);
      logf("LAPIC Addr Override: lapic_addr=%lx\n", lapic_addr);
    } else {
      logf("Unknown/short. Raw bytes:\n");
      dump_bytes(e, len);
    }

    off += len;
  }

  logf("[MADT] done\n");
}

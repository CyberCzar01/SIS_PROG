#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct __attribute__((packed)) {
  char     signature[8];
  uint8_t  checksum;
  char     oemid[6];
  uint8_t  revision;
  uint32_t rsdt_address;
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t  extended_checksum;
  uint8_t  reserved[3];
} rsdp_t;

typedef struct __attribute__((packed)) {
  char     signature[4];
  uint32_t length;
  uint8_t  revision;
  uint8_t  checksum;
  char     oemid[6];
  char     oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} acpi_sdt_header_t;

typedef struct __attribute__((packed)) {
  acpi_sdt_header_t hdr;
  uint32_t local_apic_addr;
  uint32_t flags;
  uint8_t  entries[];
} madt_t;

void acpi_dump_rsdp(const rsdp_t* rsdp);
const madt_t* acpi_find_madt_via_rsdt(const rsdp_t* rsdp);
void acpi_dump_madt(const madt_t* madt);

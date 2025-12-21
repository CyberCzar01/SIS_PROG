#pragma once
#include <Uefi.h>

#define EI_NIDENT 16
#define ELFCLASS32 1
#define ELFDATA2LSB 1

typedef struct {
  UINT8  e_ident[EI_NIDENT];
  UINT16 e_type;
  UINT16 e_machine;
  UINT32 e_version;
  UINT32 e_entry;
  UINT32 e_phoff;
  UINT32 e_shoff;
  UINT32 e_flags;
  UINT16 e_ehsize;
  UINT16 e_phentsize;
  UINT16 e_phnum;
  UINT16 e_shentsize;
  UINT16 e_shnum;
  UINT16 e_shstrndx;
} Elf32_Ehdr;

typedef struct {
  UINT32 p_type;
  UINT32 p_offset;
  UINT32 p_vaddr;
  UINT32 p_paddr;
  UINT32 p_filesz;
  UINT32 p_memsz;
  UINT32 p_flags;
  UINT32 p_align;
} Elf32_Phdr;

#define PT_LOAD 1
#define EM_386  3

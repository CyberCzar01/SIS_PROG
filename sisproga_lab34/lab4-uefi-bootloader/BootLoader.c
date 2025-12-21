#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/Acpi.h>
#include <Guid/FileInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/DevicePathLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>

#include "Mb2.h"
#include "Elf32.h"

typedef struct {
  UINT32 KernelEntry;
  UINT32 MbInfo;
  UINT32 StackTop;
} TRAMPOLINE_PARAMS;

extern UINT8 TrampolineStart;
extern UINT8 TrampolineEnd;
extern VOID  TrampolineEntry(VOID* Params);

STATIC inline VOID Outb(UINT16 Port, UINT8 Val) { IoWrite8(Port, Val); }
STATIC inline UINT8 Inb(UINT16 Port) { return IoRead8(Port); }
#define COM1 0x3F8
STATIC VOID SerialInit(VOID) {
  Outb(COM1+1,0); Outb(COM1+3,0x80); Outb(COM1+0,1); Outb(COM1+1,0);
  Outb(COM1+3,3); Outb(COM1+2,0xC7); Outb(COM1+4,0x0B);
}
STATIC BOOLEAN TxReady(VOID){ return (Inb(COM1+5) & 0x20)!=0; }
STATIC VOID SerialPutc(CHAR8 c){
  while(!TxReady()){}
  Outb(COM1,(UINT8)c);
}
STATIC VOID SerialWrite(IN CONST CHAR8* s){
  while(*s){
    if(*s=='\n') SerialPutc('\r');
    SerialPutc(*s++);
  }
}
STATIC VOID SLog(IN CONST CHAR8* fmt, ...){
  CHAR8 buf[512];
  VA_LIST ap; VA_START(ap, fmt);
  AsciiVSPrint(buf, sizeof(buf), fmt, ap);
  VA_END(ap);
  SerialWrite(buf);
}

STATIC CHAR16* GetKernelPathFromLoadOptions(EFI_LOADED_IMAGE_PROTOCOL* Loaded) {
  if (!Loaded->LoadOptions || Loaded->LoadOptionsSize < sizeof(CHAR16)) return NULL;

  CHAR16* s = (CHAR16*)Loaded->LoadOptions;
  UINTN n = Loaded->LoadOptionsSize / sizeof(CHAR16);

  UINTN i=0;
  while(i<n && (s[i]==L' ' || s[i]==L'\t')) i++;
  while(i<n && s[i] && s[i]!=L' ' && s[i]!=L'\t') i++;
  while(i<n && (s[i]==L' ' || s[i]==L'\t')) i++;
  if (i>=n || s[i]==0) return NULL;

  UINTN start=i;
  while(i<n && s[i] && s[i]!=L' ' && s[i]!=L'\t') i++;
  UINTN len = i-start;

  CHAR16* out = AllocateZeroPool((len+1)*sizeof(CHAR16));
  if (!out) return NULL;
  CopyMem(out, &s[start], len*sizeof(CHAR16));
  out[len]=0;
  return out;
}

STATIC EFI_STATUS ValidateMb2Header(VOID* Kernel, UINTN Size) {
  UINT8* p = (UINT8*)Kernel;
  UINTN max = (Size < 32768) ? Size : 32768;

  for (UINTN off=0; off + sizeof(MB2_HEADER) <= max; off += 8) {
    MB2_HEADER* h = (MB2_HEADER*)(p + off);
    if (h->magic != MB2_HEADER_MAGIC) continue;

    UINT32 sum = h->magic + h->architecture + h->header_length + h->checksum;
    if (sum != 0) return EFI_COMPROMISED_DATA;
    if (h->architecture != 0) return EFI_UNSUPPORTED;
    if (h->header_length < sizeof(MB2_HEADER) || off + h->header_length > max) return EFI_COMPROMISED_DATA;

    UINT8* t = (UINT8*)h + sizeof(MB2_HEADER);
    UINT8* tend = (UINT8*)h + h->header_length;
    BOOLEAN hasEnd = FALSE;

    while (t + sizeof(MB2_HEADER_TAG) <= tend) {
      MB2_HEADER_TAG* tag = (MB2_HEADER_TAG*)t;
      if (tag->type == 0 && tag->size == 8) { hasEnd = TRUE; break; }
      if (tag->size < sizeof(MB2_HEADER_TAG)) return EFI_COMPROMISED_DATA;
      t += MB2_ALIGN8(tag->size);
    }
    if (!hasEnd) return EFI_COMPROMISED_DATA;

    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

#define PAGE_4K 4096u
#define ALIGN_DOWN(x,a) ((UINT32)((x) & ~((a)-1)))
#define ALIGN_UP(x,a)   ((UINT32)(((x) + ((a)-1)) & ~((a)-1)))

STATIC EFI_STATUS LoadElf32SegmentsBelow4G(VOID* Kernel, UINTN Size, UINT32* OutEntry) {
  if (Size < sizeof(Elf32_Ehdr)) return EFI_LOAD_ERROR;
  Elf32_Ehdr* eh = (Elf32_Ehdr*)Kernel;

  if (eh->e_ident[0]!=0x7F || eh->e_ident[1]!='E' || eh->e_ident[2]!='L' || eh->e_ident[3]!='F') return EFI_UNSUPPORTED;
  if (eh->e_ident[4] != ELFCLASS32) return EFI_UNSUPPORTED;
  if (eh->e_ident[5] != ELFDATA2LSB) return EFI_UNSUPPORTED;
  if (eh->e_machine != EM_386) return EFI_UNSUPPORTED;

  if (eh->e_phoff + (UINTN)eh->e_phnum * eh->e_phentsize > Size) return EFI_COMPROMISED_DATA;

  UINT32 minBase = 0xFFFFFFFFu;
  UINT32 maxEnd  = 0;

  for (UINTN i = 0; i < eh->e_phnum; i++) {
    Elf32_Phdr* ph = (Elf32_Phdr*)((UINT8*)Kernel + eh->e_phoff + i * eh->e_phentsize);
    if (ph->p_type != PT_LOAD) continue;

    if ((UINT64)ph->p_paddr + (UINT64)ph->p_memsz > 0xFFFFFFFFull) return EFI_UNSUPPORTED;
    if ((UINT64)ph->p_offset + (UINT64)ph->p_filesz > (UINT64)Size) return EFI_COMPROMISED_DATA;

    UINT32 segBase = ALIGN_DOWN((UINT32)ph->p_paddr, PAGE_4K);
    UINT32 segEnd  = ALIGN_UP((UINT32)ph->p_paddr + (UINT32)ph->p_memsz, PAGE_4K);

    if (segBase < minBase) minBase = segBase;
    if (segEnd  > maxEnd)  maxEnd  = segEnd;

    Print(L"[ELF] PH%u LOAD paddr=%08x mem=%08x file=%08x off=%08x\n",
          (unsigned)i, (UINT32)ph->p_paddr, (UINT32)ph->p_memsz, (UINT32)ph->p_filesz, (UINT32)ph->p_offset);
  }

  if (minBase == 0xFFFFFFFFu || maxEnd <= minBase) return EFI_LOAD_ERROR;

  UINT32 totalBytes = maxEnd - minBase;
  UINTN totalPages  = EFI_SIZE_TO_PAGES(totalBytes);

  Print(L"[ELF] Reserve range base=%08x end=%08x bytes=%u pages=%u\n",
        minBase, maxEnd, (unsigned)totalBytes, (unsigned)totalPages);

  EFI_PHYSICAL_ADDRESS dst = (EFI_PHYSICAL_ADDRESS)minBase;
  EFI_STATUS st = gBS->AllocatePages(AllocateAddress, EfiLoaderData, totalPages, &dst);
  if (EFI_ERROR(st)) {
    Print(L"[ELF][ERR] AllocatePages(AllocateAddress, base=%08x pages=%u): %r\n",
          minBase, (unsigned)totalPages, st);
    return st;
  }

  
  for (UINTN i = 0; i < eh->e_phnum; i++) {
    Elf32_Phdr* ph = (Elf32_Phdr*)((UINT8*)Kernel + eh->e_phoff + i * eh->e_phentsize);
    if (ph->p_type != PT_LOAD) continue;

    UINT32 paddr = (UINT32)ph->p_paddr;

    
    SetMem((VOID*)(UINTN)paddr, ph->p_memsz, 0);

    
    CopyMem((VOID*)(UINTN)paddr, (UINT8*)Kernel + ph->p_offset, ph->p_filesz);
  }

  *OutEntry = eh->e_entry;
  Print(L"[ELF] Entry=%08x\n", (UINT32)*OutEntry);
  return EFI_SUCCESS;
}


STATIC EFI_STATUS BuildMb2InfoBelow4G(EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop, VOID** OutMb, UINT32* OutMbPhys) {
  EFI_CONFIGURATION_TABLE* ct = gST->ConfigurationTable;
  UINTN n = gST->NumberOfTableEntries;

  VOID* Rsdp = NULL;
  BOOLEAN Acpi2 = FALSE;
  for (UINTN i=0;i<n;i++) {
    if (CompareGuid(&ct[i].VendorGuid, &gEfiAcpi20TableGuid)) { Rsdp = ct[i].VendorTable; Acpi2 = TRUE; break; }
  }
  if (!Rsdp) {
    for (UINTN i=0;i<n;i++) {
      if (CompareGuid(&ct[i].VendorGuid, &gEfiAcpi10TableGuid)) { Rsdp = ct[i].VendorTable; Acpi2 = FALSE; break; }
    }
  }
  if (!Rsdp) return EFI_NOT_FOUND;

  UINT8* rsdp8 = (UINT8*)Rsdp;
  UINTN rsdpLen = Acpi2 ? 36 : 20;

  EFI_PHYSICAL_ADDRESS max = 0xFFFFFFFFull;
  UINTN pages = EFI_SIZE_TO_PAGES(4096);
  EFI_STATUS st = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &max);
  if (EFI_ERROR(st)) return st;

  UINT8* buf = (UINT8*)(UINTN)max;
  SetMem(buf, 4096, 0);

  MB2_INFO* info = (MB2_INFO*)buf;
  UINT32 off = sizeof(MB2_INFO);

  {
    MB2_TAG_STRING* t = (MB2_TAG_STRING*)(buf + off);
    t->tag.type = 2;
    CONST CHAR8* name = "Lab4BootLoader";
    UINT32 sl = (UINT32)AsciiStrLen(name) + 1;
    t->tag.size = sizeof(MB2_TAG) + sl;
    CopyMem(t->string, name, sl);
    off += MB2_ALIGN8(t->tag.size);
  }

  {
    MB2_TAG_FRAMEBUFFER* t = (MB2_TAG_FRAMEBUFFER*)(buf + off);
    t->tag.type = 8;
    t->tag.size = sizeof(MB2_TAG_FRAMEBUFFER);

    t->framebuffer_addr   = (UINT64)Gop->Mode->FrameBufferBase;
    t->framebuffer_pitch  = (UINT32)(Gop->Mode->Info->PixelsPerScanLine * 4);
    t->framebuffer_width  = (UINT32)Gop->Mode->Info->HorizontalResolution;
    t->framebuffer_height = (UINT32)Gop->Mode->Info->VerticalResolution;
    t->framebuffer_bpp    = 32;
    t->framebuffer_type   = 1; 

  
    t->red_field_position   = 16; t->red_mask_size   = 8;
    t->green_field_position = 8;  t->green_mask_size = 8;
    t->blue_field_position  = 0;  t->blue_mask_size  = 8;

    off += MB2_ALIGN8(t->tag.size);
  }

  {
    MB2_TAG* t = (MB2_TAG*)(buf + off);
    t->type = Acpi2 ? 15 : 14;
    t->size = (UINT32)(sizeof(MB2_TAG) + rsdpLen);
    CopyMem((UINT8*)t + sizeof(MB2_TAG), rsdp8, rsdpLen);
    off += MB2_ALIGN8(t->size);
  }

  {
    MB2_TAG* t = (MB2_TAG*)(buf + off);
    t->type = 0; t->size = 8;
    off += 8;
  }

  info->total_size = off;
  info->reserved = 0;

  *OutMb = buf;
  *OutMbPhys = (UINT32)(UINTN)max;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS ExitBootServicesSafe(EFI_HANDLE ImageHandle) {
  EFI_STATUS st;
  UINTN mapSize = 0, mapKey = 0, descSize = 0;
  UINT32 descVer = 0;

  for (UINTN attempt = 0; attempt < 16; attempt++) {
    mapSize = 0;
    st = gBS->GetMemoryMap(&mapSize, NULL, &mapKey, &descSize, &descVer);
    if (st != EFI_BUFFER_TOO_SMALL) return st;

    mapSize += 2 * descSize;
    VOID* map = AllocatePool(mapSize);
    if (!map) return EFI_OUT_OF_RESOURCES;

    st = gBS->GetMemoryMap(&mapSize, map, &mapKey, &descSize, &descVer);
    if (EFI_ERROR(st)) {
      FreePool(map);
      return st;
    }

    st = gBS->ExitBootServices(ImageHandle, mapKey);

    if (!EFI_ERROR(st)) {
      return EFI_SUCCESS;
    }

    FreePool(map);

    if (st != EFI_INVALID_PARAMETER) return st;
  }

  return EFI_INVALID_PARAMETER;
}

static EFI_STATUS ReadFileToBufferAnyFs(IN CHAR16* Path, OUT VOID** OutBuf, OUT UINTN* OutSize) {
  EFI_STATUS st;
  EFI_HANDLE* Handles = NULL;
  UINTN HandleCount = 0;

  if (!OutBuf || !OutSize) return EFI_INVALID_PARAMETER;
  *OutBuf = NULL;
  *OutSize = 0;

  st = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR(st)) {
    Print(L"[BL][ERR] LocateHandleBuffer(SFS): %r\n", st);
    return st;
  }

  Print(L"[BL] Found %u SimpleFS handles\n", (unsigned)HandleCount);

  for (UINTN hi = 0; hi < HandleCount; hi++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* Sfs = NULL;
    st = gBS->HandleProtocol(Handles[hi], &gEfiSimpleFileSystemProtocolGuid, (VOID**)&Sfs);
    if (EFI_ERROR(st) || !Sfs) continue;

    EFI_FILE_PROTOCOL* Root = NULL;
    st = Sfs->OpenVolume(Sfs, &Root);
    if (EFI_ERROR(st) || !Root) continue;

    CHAR16* Try1 = Path;
    CHAR16* Try2 = (Path && Path[0] == L'\\') ? (Path + 1) : Path;

    EFI_FILE_PROTOCOL* File = NULL;
    st = Root->Open(Root, &File, Try1, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st) && Try2 != Try1) {
      st = Root->Open(Root, &File, Try2, EFI_FILE_MODE_READ, 0);
    }
    if (EFI_ERROR(st) || !File) {
      Root->Close(Root);
      continue;
    }

    UINTN InfoSize = 0;
    st = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, NULL);
    if (st != EFI_BUFFER_TOO_SMALL) {
      Print(L"[BL][ERR] GetInfo size probe: %r\n", st);
      File->Close(File);
      Root->Close(Root);
      continue;
    }

    EFI_FILE_INFO* Info = AllocatePool(InfoSize);
    if (!Info) {
      File->Close(File);
      Root->Close(Root);
      FreePool(Handles);
      return EFI_OUT_OF_RESOURCES;
    }

    st = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, Info);
    if (EFI_ERROR(st)) {
      Print(L"[BL][ERR] GetInfo: %r\n", st);
      FreePool(Info);
      File->Close(File);
      Root->Close(Root);
      continue;
    }

    UINTN Size = (UINTN)Info->FileSize;
    FreePool(Info);

    if (Size == 0) {
      Print(L"[BL][WARN] Found file but size=0 on handle #%u\n", (unsigned)hi);
      File->Close(File);
      Root->Close(Root);
      continue;
    }

    VOID* Buf = AllocatePool(Size);
    if (!Buf) {
      File->Close(File);
      Root->Close(Root);
      FreePool(Handles);
      return EFI_OUT_OF_RESOURCES;
    }

    UINTN ReadSize = Size;
    st = File->Read(File, &ReadSize, Buf);

    File->Close(File);
    Root->Close(Root);

    if (EFI_ERROR(st)) {
      Print(L"[BL][ERR] Read: %r\n", st);
      FreePool(Buf);
      continue;
    }
    if (ReadSize != Size) {
      Print(L"[BL][ERR] ReadSize mismatch: got=%u exp=%u\n", (unsigned)ReadSize, (unsigned)Size);
      FreePool(Buf);
      continue;
    }

    Print(L"[BL] kernel.bin read OK: %u bytes (from FS handle #%u)\n", (unsigned)Size, (unsigned)hi);

    *OutBuf = Buf;
    *OutSize = Size;
    FreePool(Handles);
    return EFI_SUCCESS;
  }

  FreePool(Handles);
  return EFI_NOT_FOUND;
}

typedef VOID (EFIAPI *TRAMP_FUNC)(VOID* Params);

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
  (void)SystemTable;
  SerialInit();
  SLog("\n=== LAB4 BootLoader start ===\n");

  EFI_LOADED_IMAGE_PROTOCOL* Loaded;
  EFI_STATUS st = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&Loaded);
  if (EFI_ERROR(st)) return st;

  CHAR16* KernelPath = GetKernelPathFromLoadOptions(Loaded);

  Print(L"BootLoader: kernel path: %s\n", KernelPath ? KernelPath : L"\\kernel.bin");
  SLog("[BL] kernel path: %ls\n", KernelPath ? KernelPath : L"\\kernel.bin");

  VOID* KernelBuf = NULL;
  UINTN KernelSize = 0;

  st = ReadFileToBufferAnyFs(KernelPath ? KernelPath : L"\\kernel.bin", &KernelBuf, &KernelSize);
  Print(L"[BL] ReadFileToBufferAnyFs: %r size=%u\n", st, (unsigned)KernelSize);
  if (EFI_ERROR(st)) {
    Print(L"[BL][FATAL] can't read kernel.bin\n");
    return st;
  }

  st = ValidateMb2Header(KernelBuf, KernelSize);
  if (EFI_ERROR(st)) { Print(L"MB2 header invalid: %r\n", st); SLog("[BL][ERR] mb2 header invalid\n"); return st; }
  SLog("[BL] MB2 header OK\n");

  UINT32 Entry = 0;
  st = LoadElf32SegmentsBelow4G(KernelBuf, KernelSize, &Entry);
  if (EFI_ERROR(st)) { Print(L"ELF load failed: %r\n", st); SLog("[BL][ERR] elf load: %r\n", st); return st; }
  Print(L"[BL] Entry (from main) = %08x\n", Entry);
  SLog("[BL] ELF loaded. entry=%08x\n", Entry);

  EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop;
  st = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&Gop);
  if (EFI_ERROR(st)) { Print(L"GOP not found: %r\n", st); SLog("[BL][ERR] GOP not found\n"); return st; }
  Print(L"[BL] GOP: %ux%u fb=%lx\n",
        (UINT32)Gop->Mode->Info->HorizontalResolution,
        (UINT32)Gop->Mode->Info->VerticalResolution,
        (UINT64)Gop->Mode->FrameBufferBase);
  SLog("[BL] GOP: %ux%u fb=%lx\n",
       (UINT32)Gop->Mode->Info->HorizontalResolution,
       (UINT32)Gop->Mode->Info->VerticalResolution,
       (UINT64)Gop->Mode->FrameBufferBase);

  VOID* MbInfo = NULL;
  UINT32 MbInfoPhys = 0;
  st = BuildMb2InfoBelow4G(Gop, &MbInfo, &MbInfoPhys);
  if (EFI_ERROR(st)) { Print(L"MB info build failed: %r\n", st); SLog("[BL][ERR] mbinfo build\n"); return st; }
  Print(L"[BL] MbInfoPhys=%08x\n", MbInfoPhys);
  SLog("[BL] MBINFO @ %08x\n", MbInfoPhys);

  EFI_PHYSICAL_ADDRESS max = 0xFFFFFFFFull;
  st = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(4096), &max);
  if (EFI_ERROR(st)) return st;
  TRAMPOLINE_PARAMS* Params = (TRAMPOLINE_PARAMS*)(UINTN)max;

  EFI_PHYSICAL_ADDRESS stackMax = 0xFFFFFFFFull;
  st = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(16384), &stackMax);
  if (EFI_ERROR(st)) return st;

  Params->KernelEntry = Entry;
  Params->MbInfo = MbInfoPhys;
  Params->StackTop = (UINT32)((UINTN)stackMax + 16384);

  UINTN trampSize = (UINTN)(&TrampolineEnd - &TrampolineStart);
  EFI_PHYSICAL_ADDRESS trampMax = 0xFFFFFFFFull;
  st = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(trampSize), &trampMax);
  if (EFI_ERROR(st)) return st;
  CopyMem((VOID*)(UINTN)trampMax, &TrampolineStart, trampSize);

  Print(L"[BL] TrampolineStart=%p TrampolineEntry=%p TrampolineEnd=%p\n",
        &TrampolineStart, &TrampolineEntry, &TrampolineEnd);
  Print(L"[BL] trampMax=%lx trampSize=%u\n", (UINT64)trampMax, (UINT32)trampSize);
  UINT8* tb = (UINT8*)(UINTN)trampMax;
  Print(L"[BL] tramp bytes:");
  for (UINTN i=0;i<16;i++) Print(L" %02x", tb[i]);
  Print(L"\n");

  SLog("[BL] ExitBootServices...\n");
  st = ExitBootServicesSafe(ImageHandle);
  if (EFI_ERROR(st)) {
    Print(L"[BL][ERR] ExitBootServices: %r\n", st);
    SLog("[BL][ERR] ExitBootServices: 0x%lx\n", (UINT64)st);
    return st;
  }

  typedef VOID (EFIAPI *TRAMP_FUNC)(VOID* Params);
  TRAMP_FUNC Tramp = (TRAMP_FUNC)(UINTN)trampMax;
  Tramp(Params);

  return EFI_SUCCESS;
}

#include <Uefi.h>

#include <Guid/Acpi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

#include "Elf32.h"
#include "Mb2.h"

typedef struct {
  UINT32 KernelEntry;
  UINT32 MbInfo;
  UINT32 StackTop;
} TRAMPOLINE_PARAMS;

extern UINT8 TrampolineStart;
extern UINT8 TrampolineEnd;
extern VOID  TrampolineEntry(VOID *Params);

STATIC CHAR16 *
GetKernelPathFromLoadOptions(EFI_LOADED_IMAGE_PROTOCOL *Loaded)
{
  if (Loaded == NULL || Loaded->LoadOptions == NULL || Loaded->LoadOptionsSize < sizeof(CHAR16)) {
    return NULL;
  }

  CHAR16 *s = (CHAR16 *)Loaded->LoadOptions;
  UINTN  n  = Loaded->LoadOptionsSize / sizeof(CHAR16);

  UINTN i = 0;
  while (i < n && (s[i] == L' ' || s[i] == L'\t')) {
    i++;
  }
  while (i < n && s[i] != 0 && s[i] != L' ' && s[i] != L'\t') {
    i++;
  }
  while (i < n && (s[i] == L' ' || s[i] == L'\t')) {
    i++;
  }

  if (i >= n || s[i] == 0) {
    return NULL;
  }

  UINTN start = i;
  while (i < n && s[i] != 0 && s[i] != L' ' && s[i] != L'\t') {
    i++;
  }

  UINTN len = i - start;
  CHAR16 *out = AllocateZeroPool((len + 1) * sizeof(CHAR16));
  if (out == NULL) {
    return NULL;
  }

  CopyMem(out, &s[start], len * sizeof(CHAR16));
  out[len] = 0;
  return out;
}

STATIC EFI_STATUS
GetFileSizeNoGuid(EFI_FILE_PROTOCOL *File, UINTN *OutSize)
{
  if (File == NULL || OutSize == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Seek to end, read position, seek back.
  EFI_STATUS st = File->SetPosition(File, 0xFFFFFFFFFFFFFFFFULL);
  if (EFI_ERROR(st)) {
    return st;
  }

  UINT64 pos = 0;
  st = File->GetPosition(File, &pos);
  if (EFI_ERROR(st)) {
    return st;
  }

  st = File->SetPosition(File, 0);
  if (EFI_ERROR(st)) {
    return st;
  }

  if (pos > MAX_UINTN) {
    return EFI_UNSUPPORTED;
  }

  *OutSize = (UINTN)pos;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadFileToBufferAnyFs(IN CHAR16 *Path, OUT VOID **OutBuf, OUT UINTN *OutSize)
{
  if (OutBuf == NULL || OutSize == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *OutBuf  = NULL;
  *OutSize = 0;

  EFI_STATUS st;
  EFI_HANDLE *Handles     = NULL;
  UINTN      HandleCount  = 0;

  st = gBS->LocateHandleBuffer(
            ByProtocol,
            &gEfiSimpleFileSystemProtocolGuid,
            NULL,
            &HandleCount,
            &Handles
            );
  if (EFI_ERROR(st)) {
    DEBUG((DEBUG_ERROR, "[BL] LocateHandleBuffer(SFS): %r\n", st));
    return st;
  }

  DEBUG((DEBUG_INFO, "[BL] Found %u SimpleFS handles\n", (UINT32)HandleCount));

  for (UINTN hi = 0; hi < HandleCount; hi++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Sfs = NULL;
    st = gBS->HandleProtocol(Handles[hi], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Sfs);
    if (EFI_ERROR(st) || Sfs == NULL) {
      continue;
    }

    EFI_FILE_PROTOCOL *Root = NULL;
    st = Sfs->OpenVolume(Sfs, &Root);
    if (EFI_ERROR(st) || Root == NULL) {
      continue;
    }

    CHAR16 *Try1 = Path;
    CHAR16 *Try2 = NULL;
    if (Path != NULL && Path[0] == L'\\') {
      Try2 = Path + 1;
    }

    EFI_FILE_PROTOCOL *File = NULL;
    st = Root->Open(Root, &File, Try1, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st) && Try2 != NULL) {
      st = Root->Open(Root, &File, Try2, EFI_FILE_MODE_READ, 0);
    }

    if (EFI_ERROR(st) || File == NULL) {
      Root->Close(Root);
      continue;
    }

    UINTN Size = 0;
    st = GetFileSizeNoGuid(File, &Size);
    if (EFI_ERROR(st) || Size == 0) {
      DEBUG((DEBUG_ERROR, "[BL] GetFileSize failed: %r size=%u\n", st, (UINT32)Size));
      File->Close(File);
      Root->Close(Root);
      continue;
    }

    VOID *Buf = AllocatePool(Size);
    if (Buf == NULL) {
      File->Close(File);
      Root->Close(Root);
      FreePool(Handles);
      return EFI_OUT_OF_RESOURCES;
    }

    UINTN ReadSize = Size;
    st = File->Read(File, &ReadSize, Buf);

    File->Close(File);
    Root->Close(Root);

    if (EFI_ERROR(st) || ReadSize != Size) {
      DEBUG((DEBUG_ERROR, "[BL] Read failed: %r read=%u exp=%u\n", st, (UINT32)ReadSize, (UINT32)Size));
      FreePool(Buf);
      continue;
    }

    DEBUG((DEBUG_INFO, "[BL] kernel.bin read OK: %u bytes\n", (UINT32)Size));
    *OutBuf  = Buf;
    *OutSize = Size;

    FreePool(Handles);
    return EFI_SUCCESS;
  }

  FreePool(Handles);
  return EFI_NOT_FOUND;
}

STATIC EFI_STATUS
ValidateMb2Header(VOID *Kernel, UINTN Size)
{
  UINT8 *p   = (UINT8 *)Kernel;
  UINTN max  = (Size < 32768) ? Size : 32768;

  for (UINTN off = 0; off + sizeof(MB2_HEADER) <= max; off += 8) {
    MB2_HEADER *h = (MB2_HEADER *)(p + off);
    if (h->magic != MB2_HEADER_MAGIC) {
      continue;
    }

    UINT32 sum = h->magic + h->architecture + h->header_length + h->checksum;
    if (sum != 0) {
      return EFI_COMPROMISED_DATA;
    }
    if (h->architecture != 0) {
      return EFI_UNSUPPORTED;
    }
    if (h->header_length < sizeof(MB2_HEADER) || off + h->header_length > max) {
      return EFI_COMPROMISED_DATA;
    }

    UINT8   *t     = (UINT8 *)h + sizeof(MB2_HEADER);
    UINT8   *tend  = (UINT8 *)h + h->header_length;
    BOOLEAN hasEnd = FALSE;

    while (t + sizeof(MB2_HEADER_TAG) <= tend) {
      MB2_HEADER_TAG *tag = (MB2_HEADER_TAG *)t;
      if (tag->type == 0 && tag->size == 8) {
        hasEnd = TRUE;
        break;
      }
      if (tag->size < sizeof(MB2_HEADER_TAG)) {
        return EFI_COMPROMISED_DATA;
      }
      t += MB2_ALIGN8(tag->size);
    }

    return hasEnd ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
  }

  return EFI_NOT_FOUND;
}

#define PAGE_4K 4096u
#define ALIGN_DOWN(x,a) ((UINT32)((x) & ~((a)-1)))
#define ALIGN_UP(x,a)   ((UINT32)(((x) + ((a)-1)) & ~((a)-1)))

STATIC EFI_STATUS
LoadElf32SegmentsBelow4G(VOID *Kernel, UINTN Size, UINT32 *OutEntry)
{
  if (Kernel == NULL || OutEntry == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (Size < sizeof(Elf32_Ehdr)) {
    return EFI_LOAD_ERROR;
  }

  Elf32_Ehdr *eh = (Elf32_Ehdr *)Kernel;

  if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
    return EFI_UNSUPPORTED;
  }
  if (eh->e_ident[4] != ELFCLASS32) {
    return EFI_UNSUPPORTED;
  }
  if (eh->e_ident[5] != ELFDATA2LSB) {
    return EFI_UNSUPPORTED;
  }
  if (eh->e_machine != EM_386) {
    return EFI_UNSUPPORTED;
  }

  if (eh->e_phoff + (UINTN)eh->e_phnum * eh->e_phentsize > Size) {
    return EFI_COMPROMISED_DATA;
  }

  UINT32 minBase = 0xFFFFFFFFu;
  UINT32 maxEnd  = 0;

  for (UINTN i = 0; i < eh->e_phnum; i++) {
    Elf32_Phdr *ph = (Elf32_Phdr *)((UINT8 *)Kernel + eh->e_phoff + i * eh->e_phentsize);
    if (ph->p_type != PT_LOAD) {
      continue;
    }

    if ((UINT64)ph->p_paddr + (UINT64)ph->p_memsz > 0xFFFFFFFFull) {
      return EFI_UNSUPPORTED;
    }
    if ((UINT64)ph->p_offset + (UINT64)ph->p_filesz > (UINT64)Size) {
      return EFI_COMPROMISED_DATA;
    }

    UINT32 segBase = ALIGN_DOWN((UINT32)ph->p_paddr, PAGE_4K);
    UINT32 segEnd  = ALIGN_UP((UINT32)ph->p_paddr + (UINT32)ph->p_memsz, PAGE_4K);

    if (segBase < minBase) {
      minBase = segBase;
    }
    if (segEnd > maxEnd) {
      maxEnd = segEnd;
    }

    DEBUG((DEBUG_INFO, "[ELF] PH%u LOAD paddr=%08x mem=%08x file=%08x off=%08x\n",
           (UINT32)i, (UINT32)ph->p_paddr, (UINT32)ph->p_memsz, (UINT32)ph->p_filesz, (UINT32)ph->p_offset));
  }

  if (minBase == 0xFFFFFFFFu || maxEnd <= minBase) {
    return EFI_LOAD_ERROR;
  }

  UINT32 totalBytes = maxEnd - minBase;
  UINTN  totalPages = EFI_SIZE_TO_PAGES(totalBytes);

  DEBUG((DEBUG_INFO, "[ELF] Reserve range base=%08x end=%08x bytes=%u pages=%u\n",
         minBase, maxEnd, totalBytes, (UINT32)totalPages));

  EFI_PHYSICAL_ADDRESS dst = (EFI_PHYSICAL_ADDRESS)minBase;
  EFI_STATUS st = gBS->AllocatePages(AllocateAddress, EfiLoaderData, totalPages, &dst);
  if (EFI_ERROR(st)) {
    DEBUG((DEBUG_ERROR, "[ELF] AllocatePages failed: %r\n", st));
    return st;
  }

  for (UINTN i = 0; i < eh->e_phnum; i++) {
    Elf32_Phdr *ph = (Elf32_Phdr *)((UINT8 *)Kernel + eh->e_phoff + i * eh->e_phentsize);
    if (ph->p_type != PT_LOAD) {
      continue;
    }

    UINT32 paddr = (UINT32)ph->p_paddr;

    SetMem((VOID *)(UINTN)paddr, ph->p_memsz, 0);
    CopyMem((VOID *)(UINTN)paddr, (UINT8 *)Kernel + ph->p_offset, ph->p_filesz);
  }

  *OutEntry = eh->e_entry;
  DEBUG((DEBUG_INFO, "[ELF] Entry=%08x\n", *OutEntry));
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
BuildMb2InfoBelow4G(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, VOID **OutMb, UINT32 *OutMbPhys)
{
  if (Gop == NULL || OutMb == NULL || OutMbPhys == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  EFI_CONFIGURATION_TABLE *ct = gST->ConfigurationTable;
  UINTN n = gST->NumberOfTableEntries;

  VOID   *Rsdp  = NULL;
  BOOLEAN Acpi2 = FALSE;

  for (UINTN i = 0; i < n; i++) {
    if (CompareGuid(&ct[i].VendorGuid, &gEfiAcpi20TableGuid)) {
      Rsdp = ct[i].VendorTable;
      Acpi2 = TRUE;
      break;
    }
  }
  if (Rsdp == NULL) {
    for (UINTN i = 0; i < n; i++) {
      if (CompareGuid(&ct[i].VendorGuid, &gEfiAcpi10TableGuid)) {
        Rsdp = ct[i].VendorTable;
        Acpi2 = FALSE;
        break;
      }
    }
  }
  if (Rsdp == NULL) {
    return EFI_NOT_FOUND;
  }

  UINTN rsdpLen = Acpi2 ? 36 : 20;

  EFI_PHYSICAL_ADDRESS max = 0xFFFFFFFFull;
  EFI_STATUS st = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(4096), &max);
  if (EFI_ERROR(st)) {
    return st;
  }

  UINT8 *buf = (UINT8 *)(UINTN)max;
  SetMem(buf, 4096, 0);

  MB2_INFO *info = (MB2_INFO *)buf;
  UINT32 off = sizeof(MB2_INFO);

  // Bootloader name tag (type=2)
  {
    MB2_TAG_STRING *t = (MB2_TAG_STRING *)(buf + off);
    t->tag.type = 2;
    CONST CHAR8 *name = "Lab4BootLoader";
    UINT32 sl = (UINT32)AsciiStrLen(name) + 1;
    t->tag.size = sizeof(MB2_TAG) + sl;
    CopyMem(t->string, name, sl);
    off += MB2_ALIGN8(t->tag.size);
  }

  // Framebuffer tag (type=8)
  {
    MB2_TAG_FRAMEBUFFER *t = (MB2_TAG_FRAMEBUFFER *)(buf + off);
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

  // ACPI tag (type=14 or 15)
  {
    MB2_TAG *t = (MB2_TAG *)(buf + off);
    t->type = Acpi2 ? 15 : 14;
    t->size = (UINT32)(sizeof(MB2_TAG) + rsdpLen);
    CopyMem((UINT8 *)t + sizeof(MB2_TAG), Rsdp, rsdpLen);
    off += MB2_ALIGN8(t->size);
  }

  // End tag
  {
    MB2_TAG *t = (MB2_TAG *)(buf + off);
    t->type = 0;
    t->size = 8;
    off += 8;
  }

  info->total_size = off;
  info->reserved   = 0;

  *OutMb     = buf;
  *OutMbPhys = (UINT32)(UINTN)max;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ExitBootServicesSafe(EFI_HANDLE ImageHandle)
{
  EFI_STATUS st;
  UINTN mapKey = 0, descSize = 0, mapSize = 0;
  UINT32 descVer = 0;

  for (UINTN attempt = 0; attempt < 16; attempt++) {
    mapSize = 0;
    st = gBS->GetMemoryMap(&mapSize, NULL, &mapKey, &descSize, &descVer);
    if (st != EFI_BUFFER_TOO_SMALL) {
      return st;
    }

    mapSize += 2 * descSize;
    VOID *map = AllocatePool(mapSize);
    if (map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    st = gBS->GetMemoryMap(&mapSize, map, &mapKey, &descSize, &descVer);
    if (EFI_ERROR(st)) {
      FreePool(map);
      return st;
    }

    st = gBS->ExitBootServices(ImageHandle, mapKey);

    // ВАЖНО: если ExitBootServices успешен — НЕ вызываем FreePool!
    // После успешного ExitBootServices все Boot Services недоступны
    if (!EFI_ERROR(st)) {
      return EFI_SUCCESS;
    }

    // Если неудача — можно освобождать память и пробовать снова
    FreePool(map);

    if (st != EFI_INVALID_PARAMETER) {
      return st;
    }
  }

  return EFI_INVALID_PARAMETER;
}

typedef VOID (EFIAPI *TRAMP_FUNC)(VOID *Params);

EFI_STATUS EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  (void)SystemTable;

  DEBUG((DEBUG_INFO, "\n=== LAB4 BootLoader start ===\n"));

  EFI_LOADED_IMAGE_PROTOCOL *Loaded = NULL;
  EFI_STATUS st = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR(st) || Loaded == NULL) {
    DEBUG((DEBUG_ERROR, "[BL] HandleProtocol(LoadedImage): %r\n", st));
    return st;
  }

  CHAR16 *KernelPath = GetKernelPathFromLoadOptions(Loaded);
  if (KernelPath == NULL) {
    KernelPath = L"\\kernel.bin";
  }

  Print(L"BootLoader: kernel path: %s\n", KernelPath);
  DEBUG((DEBUG_INFO, "[BL] kernel path: %s\n", KernelPath));

  VOID *KernelBuf = NULL;
  UINTN KernelSize = 0;

  st = ReadFileToBufferAnyFs(KernelPath, &KernelBuf, &KernelSize);
  if (EFI_ERROR(st)) {
    Print(L"[BL][FATAL] can't read kernel.bin: %r\n", st);
    DEBUG((DEBUG_ERROR, "[BL] can't read kernel.bin: %r\n", st));
    return st;
  }

  st = ValidateMb2Header(KernelBuf, KernelSize);
  if (EFI_ERROR(st)) {
    Print(L"[BL][FATAL] MB2 header invalid: %r\n", st);
    DEBUG((DEBUG_ERROR, "[BL] MB2 header invalid: %r\n", st));
    return st;
  }

  UINT32 Entry = 0;
  st = LoadElf32SegmentsBelow4G(KernelBuf, KernelSize, &Entry);
  if (EFI_ERROR(st)) {
    Print(L"[BL][FATAL] ELF load failed: %r\n", st);
    DEBUG((DEBUG_ERROR, "[BL] ELF load failed: %r\n", st));
    return st;
  }

  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
  st = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR(st) || Gop == NULL) {
    Print(L"[BL][FATAL] GOP not found: %r\n", st);
    DEBUG((DEBUG_ERROR, "[BL] GOP not found: %r\n", st));
    return st;
  }

  Print(L"[BL] GOP: %ux%u fb=%lx\n",
        (UINT32)Gop->Mode->Info->HorizontalResolution,
        (UINT32)Gop->Mode->Info->VerticalResolution,
        (UINT64)Gop->Mode->FrameBufferBase);

  VOID  *MbInfo = NULL;
  UINT32 MbInfoPhys = 0;

  st = BuildMb2InfoBelow4G(Gop, &MbInfo, &MbInfoPhys);
  if (EFI_ERROR(st)) {
    Print(L"[BL][FATAL] BuildMb2Info failed: %r\n", st);
    DEBUG((DEBUG_ERROR, "[BL] BuildMb2Info failed: %r\n", st));
    return st;
  }

  DEBUG((DEBUG_INFO, "[BL] Entry=%08x MbInfo=%08x\n", Entry, MbInfoPhys));

  EFI_PHYSICAL_ADDRESS paddrParams = 0xFFFFFFFFull;
  st = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(4096), &paddrParams);
  if (EFI_ERROR(st)) {
    return st;
  }
  TRAMPOLINE_PARAMS *Params = (TRAMPOLINE_PARAMS *)(UINTN)paddrParams;

  EFI_PHYSICAL_ADDRESS paddrStack = 0xFFFFFFFFull;
  st = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(16384), &paddrStack);
  if (EFI_ERROR(st)) {
    return st;
  }

  Params->KernelEntry = Entry;
  Params->MbInfo      = MbInfoPhys;
  Params->StackTop    = (UINT32)((UINTN)paddrStack + 16384);

  UINTN trampSize = (UINTN)(&TrampolineEnd - &TrampolineStart);

  EFI_PHYSICAL_ADDRESS paddrTramp = 0xFFFFFFFFull;
  st = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(trampSize), &paddrTramp);
  if (EFI_ERROR(st)) {
    return st;
  }

  CopyMem((VOID *)(UINTN)paddrTramp, &TrampolineStart, trampSize);

  DEBUG((DEBUG_INFO, "[BL] Trampoline copied to %lx (size=%u)\n", (UINT64)paddrTramp, (UINT32)trampSize));
  DEBUG((DEBUG_INFO, "[BL] ExitBootServices...\n"));

  Print(L"[BL] Entry=%08x MbInfo=%08x\n", Entry, MbInfoPhys);
  Print(L"[BL] TrampolineStart=%p End=%p size=%u\n",
      &TrampolineStart, &TrampolineEnd, (UINT32)trampSize);
  Print(L"[BL] paddrTramp=%lx\n", (UINT64)paddrTramp);

  UINT8 *tb = (UINT8 *)(UINTN)paddrTramp;
  Print(L"[BL] tramp bytes:");
  for (UINTN i = 0; i < 16; i++) {
    Print(L" %02x", tb[i]);
  }
  Print(L"\n");

  st = ExitBootServicesSafe(ImageHandle);
  if (EFI_ERROR(st)) {
    Print(L"[BL][FATAL] ExitBootServices failed: %r\n", st);
    DEBUG((DEBUG_ERROR, "[BL] ExitBootServices failed: %r\n", st));
    return st;
  }

  TRAMP_FUNC Tramp = (TRAMP_FUNC)(UINTN)paddrTramp;
  Tramp(Params);

  // This code should never be reached - trampoline doesn't return
  return EFI_SUCCESS;
}

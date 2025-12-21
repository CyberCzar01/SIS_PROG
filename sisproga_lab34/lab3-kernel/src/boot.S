.section .multiboot2
.align 8


.set MB2_HEADER_MAGIC, 0xE85250D6
.set MB2_ARCH_I386,    0
.set MB2_HEADER_LEN,   (header_end - header_start)

header_start:
  .long MB2_HEADER_MAGIC
  .long MB2_ARCH_I386
  .long MB2_HEADER_LEN
  .long -(MB2_HEADER_MAGIC + MB2_ARCH_I386 + MB2_HEADER_LEN)

  .align 8
  .short 1          // type
  .short 0          // flags
  .long  24         // size
  .long  8          // framebuffer
  .long  14         // acpi old
  .long  15         // acpi new

  .align 8
  .short 0
  .short 0
  .long  8

header_end:

.section .text
.code32
.global _start
.extern kmain

_start:
  cli
  mov $stack_top, %esp

  push %ebx
  push %eax
  call kmain

.hang:
  hlt
  jmp .hang

.section .bss
.align 16
stack:
  .skip 16384
stack_top:

    .text
    .globl TrampolineStart
    .globl TrampolineEntry
    .globl TrampolineEnd

    .equ IA32_EFER,  0xC0000080
    .equ EFER_LME,   0x00000100
    .equ MB2_MAGIC,  0x36D76289

    .equ P_KERNEL_ENTRY, 0
    .equ P_MBINFO,       4
    .equ P_STACK_TOP,    8

TrampolineStart:
TrampolineEntry:
    cli

    movw    $0x3F8, %dx
    movb    $'A', %al
    outb    %al, (%dx)

    movl    %ecx, %esi

    subq    $16, %rsp
    leaq    gdt(%rip), %rax
    movw    $(gdt_end - gdt - 1), (%rsp)
    movq    %rax, 2(%rsp)
    lgdt    (%rsp)
    addq    $16, %rsp

    leaq    compat32(%rip), %rax
    pushq   $0x08
    pushq   %rax

    movw    $0x3F8, %dx
    movb    $'B', %al
    outb    %al, (%dx)

    lretq

    .code32
compat32:
    cli

    movl    $IA32_EFER, %ecx
    rdmsr
    andl    $~EFER_LME, %eax
    wrmsr

    movl    %cr0, %eax
    andl    $0x7FFFFFFF, %eax
    movl    %eax, %cr0

    jmp     prot32

prot32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs

    movl    P_STACK_TOP(%esi), %esp

    movl    $MB2_MAGIC, %eax
    movl    P_MBINFO(%esi), %ebx

    movl    P_KERNEL_ENTRY(%esi), %ecx
    jmp     *%ecx

    .align 8
gdt:
    .quad   0x0000000000000000
    .quad   0x00CF9A000000FFFF
    .quad   0x00CF92000000FFFF
gdt_end:

    .code64
TrampolineEnd:

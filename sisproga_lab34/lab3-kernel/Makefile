CC=gcc
AS=gcc
LD=ld
OBJCOPY=objcopy

CFLAGS=-m32 -ffreestanding -fno-pie -fno-stack-protector -O0 -g3 -Wall -Wextra \
       -nostdlib -nostdinc -fno-builtin -fno-omit-frame-pointer
ASFLAGS=-m32 -ffreestanding -O0 -g3 -Wall -Wextra -nostdlib -fno-pie
LDFLAGS=-m elf_i386 -T linker.ld -nostdlib

SRCS_C=$(wildcard src/*.c)
SRCS_S=$(wildcard src/*.S)
OBJS=$(SRCS_C:.c=.o) $(SRCS_S:.S=.o)

all: kernel.bin

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

kernel.bin: kernel.elf
	cp $< $@

clean:
	rm -f src/*.o kernel.elf kernel.bin

.PHONY: all clean

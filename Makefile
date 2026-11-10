CC      := gcc
AS      := nasm
LD      := ld
OBJCOPY := objcopy

BUILD := build

CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -Ikernel \
          -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -nostdlib
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack --build-id=none

C_SRC := $(wildcard kernel/*.c)
OBJS  := $(BUILD)/entry.o $(patsubst kernel/%.c,$(BUILD)/%.o,$(C_SRC))

QEMU      := qemu-system-i386
QEMU_DISK := -drive format=raw,file=$(BUILD)/lilos.img

.PHONY: all run run-serial clean

all: $(BUILD)/lilos.img

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/%.o: kernel/%.c kernel/lilos.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/boot.bin: bootloader/bootloader.asm $(BUILD)/kernel.bin
	$(AS) -f bin -DKERNEL_SECTORS=$$(( ($$(stat -c %s $(BUILD)/kernel.bin) + 511) / 512 )) $< -o $@

$(BUILD)/lilos.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	cat $(BUILD)/boot.bin $(BUILD)/kernel.bin > $@
	truncate -s %512 $@

run: $(BUILD)/lilos.img
	$(QEMU) $(QEMU_DISK)

run-serial: $(BUILD)/lilos.img
	$(QEMU) $(QEMU_DISK) -display none -serial stdio

clean:
	rm -rf $(BUILD)

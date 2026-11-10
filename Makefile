CC      := gcc
AS      := nasm
LD      := ld
OBJCOPY := objcopy

BUILD := build
SEED ?= $(shell date +%s)
ROUNDS ?= 60

CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -Ikernel \
          -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -fno-asynchronous-unwind-tables -fstack-clash-protection -mno-sse -mno-mmx -nostdlib
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack --build-id=none

C_SRC := $(wildcard kernel/*.c)
OBJS  := $(BUILD)/entry.o $(patsubst kernel/%.c,$(BUILD)/%.o,$(C_SRC))

QEMU      := qemu-system-i386
QEMU_DISK := -drive format=raw,file=$(BUILD)/lilos.img

.PHONY: all run run-serial test-heap test-lib fuzz-shell test clean

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

test-heap: | $(BUILD)
	$(CC) -m32 -O1 -g -DHOST_TEST -Ikernel -fsanitize=undefined \
		tests/heap_fuzz.c kernel/heap.c kernel/string.c -o $(BUILD)/heap_fuzz
	$(BUILD)/heap_fuzz

test-lib: | $(BUILD)
	gcc -O1 -g -w -Ikernel -fsanitize=address,undefined tests/lib_fuzz.c -o $(BUILD)/lib_fuzz
	$(BUILD)/lib_fuzz

fuzz-shell: all
	python3 tests/shell_fuzz.py $(SEED) $(ROUNDS) $(BUILD)/lilos.img

test: all
	python3 tests/qemu_test.py $(BUILD)/lilos.img

clean:
	rm -rf $(BUILD)

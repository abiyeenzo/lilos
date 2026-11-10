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

# Programmes du mode utilisateur : compiles a part, embarques dans le noyau
USER_PROGS := bonjour moi lire noter noyau ecrase boucle saisie attaque
USER_OBJS  := $(patsubst %,$(BUILD)/user/%.bin.o,$(USER_PROGS))
UCFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -Iuser -ffreestanding -fno-pic -fno-pie \
           -fno-stack-protector -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -nostdlib

OBJS  := $(BUILD)/entry.o $(patsubst kernel/%.c,$(BUILD)/%.o,$(C_SRC)) $(USER_OBJS)

QEMU      := qemu-system-i386
QEMU_DISK := -drive format=raw,file=$(BUILD)/lilos.img -nic user,model=rtl8139

.PHONY: all run run-serial test-heap test-lib test-fs test-sha test-net test-attack test-netattack fuzz-shell test clean

all: $(BUILD)/lilos.img

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/%.o: kernel/%.c kernel/lilos.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/user:
	mkdir -p $(BUILD)/user

$(BUILD)/user/crt0.o: user/crt0.asm | $(BUILD)/user
	$(AS) -f elf32 $< -o $@

$(BUILD)/user/%.o: user/%.c user/ulib.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD)/user/%.elf: $(BUILD)/user/crt0.o $(BUILD)/user/%.o user/user.ld
	$(LD) -m elf_i386 -T user/user.ld -nostdlib -z noexecstack --build-id=none -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/$*.o

$(BUILD)/user/%.bin: $(BUILD)/user/%.elf
	$(OBJCOPY) -O binary $< $@

# le nom des symboles _binary_build_user_<prog>_bin_* vient du chemin relatif
$(BUILD)/user/%.bin.o: $(BUILD)/user/%.bin
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

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

test-fs: | $(BUILD)
	gcc -O1 -g -w -DHOST_TEST -Ikernel -fsanitize=address,undefined \
		tests/fs_fuzz.c kernel/fs.c kernel/heap.c kernel/string.c -o $(BUILD)/fs_fuzz
	$(BUILD)/fs_fuzz

test-sha: | $(BUILD)
	gcc -O1 -g -w -Ikernel -fsanitize=address,undefined tests/sha_test.c kernel/sha256.c kernel/string.c -o $(BUILD)/sha_test
	$(BUILD)/sha_test

test-net: | $(BUILD)
	gcc -O1 -g -w -Ikernel -fsanitize=address,undefined tests/net_fuzz.c kernel/net.c kernel/string.c -o $(BUILD)/net_fuzz
	$(BUILD)/net_fuzz

test-attack: all
	python3 tests/attack_test.py $(BUILD)/lilos.img

test-netattack: all
	python3 tests/net_attack.py $(BUILD)/lilos.img

fuzz-shell: all
	python3 tests/shell_fuzz.py $(SEED) $(ROUNDS) $(BUILD)/lilos.img

test: all
	python3 tests/qemu_test.py $(BUILD)/lilos.img

clean:
	rm -rf $(BUILD)

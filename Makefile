LLVM_PREFIX := $(shell brew --prefix llvm@21 2>/dev/null)
LLD_PREFIX  := $(shell brew --prefix lld@21 2>/dev/null)

CLANG        := $(LLVM_PREFIX)/bin/clang
LD_LLD       := $(LLD_PREFIX)/bin/ld.lld
LLVM_READELF := $(LLVM_PREFIX)/bin/llvm-readelf
LLVM_OBJDUMP := $(LLVM_PREFIX)/bin/llvm-objdump
LLVM_NM      := $(LLVM_PREFIX)/bin/llvm-nm

QEMU    := qemu-system-x86_64
GDB     := gdb
XORRISO := xorriso

BUILD_DIR := build

KERNEL_SRC := kernel/kernel.c
KERNEL_OBJ := $(BUILD_DIR)/kernel.o
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
LINKER_SCRIPT := kernel/linker.ld

LIMINE_DIR := vendor/limine
LIMINE_EFI := $(LIMINE_DIR)/BOOTX64.EFI
LIMINE_UEFI_CD := $(LIMINE_DIR)/limine-uefi-cd.bin
LIMINE_VERSION_FILE := $(LIMINE_DIR)/VERSION
LIMINE_FETCH_SCRIPT := scripts/fetch-limine.sh

ISO_ROOT := $(BUILD_DIR)/iso-root
ISO_IMAGE := $(BUILD_DIR)/myos.iso

ISO_KERNEL := $(ISO_ROOT)/boot/kernel.elf
ISO_LIMINE_CONF := $(ISO_ROOT)/limine.conf
ISO_BOOTX64 := $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
ISO_LIMINE_UEFI_CD := $(ISO_ROOT)/limine-uefi-cd.bin

QEMU_FIRMWARE := $(shell brew --prefix qemu)/share/qemu/edk2-x86_64-code.fd

TARGET := x86_64-unknown-none-elf

CFLAGS := \
	--target=$(TARGET) \
	-ffreestanding \
	-fno-stack-protector \
	-fno-common \
	-mno-red-zone \
	-mcmodel=kernel \
	-Wall \
	-Wextra \
	-Werror \
	-Wpedantic

LDFLAGS := \
	-T $(LINKER_SCRIPT)

.PHONY: limine

limine: $(LIMINE_EFI) $(LIMINE_UEFI_CD)

$(LIMINE_EFI) $(LIMINE_UEFI_CD): $(LIMINE_FETCH_SCRIPT)
	./$(LIMINE_FETCH_SCRIPT)

.PHONY: all check-toolchain clean

all: $(KERNEL_ELF)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(KERNEL_OBJ): $(KERNEL_SRC) | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJ) $(LINKER_SCRIPT)
	$(LD_LLD) $(LDFLAGS) -o $@ $(KERNEL_OBJ)

.PHONY: iso

iso: $(ISO_IMAGE)

$(ISO_ROOT):
	mkdir -p $(ISO_ROOT)/boot
	mkdir -p $(ISO_ROOT)/EFI/BOOT

$(ISO_KERNEL): $(KERNEL_ELF) | $(ISO_ROOT)
	cp $(KERNEL_ELF) $(ISO_KERNEL)

$(ISO_LIMINE_CONF): limine.conf | $(ISO_ROOT)
	cp limine.conf $(ISO_LIMINE_CONF)

$(ISO_BOOTX64): $(LIMINE_EFI) | $(ISO_ROOT)
	cp $(LIMINE_EFI) $(ISO_BOOTX64)

$(ISO_LIMINE_UEFI_CD): $(LIMINE_UEFI_CD) | $(ISO_ROOT)
	cp $(LIMINE_UEFI_CD) $(ISO_LIMINE_UEFI_CD)

$(ISO_IMAGE): \
	$(ISO_KERNEL) \
	$(ISO_LIMINE_CONF) \
	$(ISO_BOOTX64) \
	$(ISO_LIMINE_UEFI_CD)
	$(XORRISO) \
		-as mkisofs \
		-R -r -J \
		-b limine-uefi-cd.bin \
		-no-emul-boot \
		-o $(ISO_IMAGE) \
		$(ISO_ROOT)

.PHONY: run

run: $(ISO_IMAGE)
	$(QEMU) \
		-machine q35 \
		-cpu qemu64 \
		-m 256M \
		-smp 1 \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) \
		-cdrom $(ISO_IMAGE) \
		-boot d \
		-no-reboot \
		-no-shutdown

.PHONY: debug

debug: $(ISO_IMAGE)
	$(QEMU) \
		-machine q35 \
		-cpu qemu64 \
		-m 256M \
		-smp 1 \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) \
		-cdrom $(ISO_IMAGE) \
		-boot d \
		-no-reboot \
		-no-shutdown \
		-S \
		-gdb tcp::1234

check-toolchain:
	@echo "=== Checking MyOS toolchain ==="
	@command -v brew >/dev/null 2>&1 || { echo "ERROR: Homebrew not found"; exit 1; }
	@test -x "$(CLANG)" || { echo "ERROR: Clang not found at $(CLANG)"; exit 1; }
	@test -x "$(LD_LLD)" || { echo "ERROR: LLD not found at $(LD_LLD)"; exit 1; }
	@test -x "$(LLVM_READELF)" || { echo "ERROR: llvm-readelf not found"; exit 1; }
	@test -x "$(LLVM_OBJDUMP)" || { echo "ERROR: llvm-objdump not found"; exit 1; }
	@test -x "$(LLVM_NM)" || { echo "ERROR: llvm-nm not found"; exit 1; }
	@command -v $(QEMU) >/dev/null 2>&1 || { echo "ERROR: QEMU not found"; exit 1; }
	@command -v $(GDB) >/dev/null 2>&1 || { echo "ERROR: GDB not found"; exit 1; }
	@command -v $(XORRISO) >/dev/null 2>&1 || { echo "ERROR: xorriso not found"; exit 1; }

	@echo
	@echo "-- Clang --"
	@$(CLANG) --version | head -n 1
	@echo
	@echo "-- LLD --"
	@$(LD_LLD) --version | head -n 1
	@echo
	@echo "-- llvm-readelf --"
	@$(LLVM_READELF) --version | head -n 1
	@echo
	@echo "-- llvm-objdump --"
	@$(LLVM_OBJDUMP) --version | head -n 1
	@echo
	@echo "-- llvm-nm --"
	@$(LLVM_NM) --version | head -n 1
	@echo
	@echo "-- QEMU --"
	@$(QEMU) --version | head -n 1
	@echo
	@echo "-- GDB --"
	@$(GDB) --version | head -n 1
	@echo
	@echo "-- xorriso --"
	@$(XORRISO) -version 2>&1 | head -n 1
	@echo
	@echo "Toolchain OK."

clean:
	rm -rf build

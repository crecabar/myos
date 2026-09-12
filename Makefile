.DEFAULT_GOAL := all

# -----------------------------------------------------------------------------
# Toolchain
# -----------------------------------------------------------------------------

LLVM_PREFIX := $(shell brew --prefix llvm@21 2>/dev/null)
LLD_PREFIX  := $(shell brew --prefix lld@21 2>/dev/null)
QEMU_PREFIX := $(shell brew --prefix qemu 2>/dev/null)

CLANG        := $(LLVM_PREFIX)/bin/clang
LD_LLD       := $(LLD_PREFIX)/bin/ld.lld
LLVM_READELF := $(LLVM_PREFIX)/bin/llvm-readelf
LLVM_OBJDUMP := $(LLVM_PREFIX)/bin/llvm-objdump
LLVM_NM      := $(LLVM_PREFIX)/bin/llvm-nm

QEMU    := qemu-system-x86_64
GDB     := gdb
XORRISO := xorriso
BEAR 	:= bear

# -----------------------------------------------------------------------------
# Third-party dependencies
# -----------------------------------------------------------------------------

LIMINE_DIR          := vendor/limine
LIMINE_EFI          := $(LIMINE_DIR)/BOOTX64.EFI
LIMINE_UEFI_CD      := $(LIMINE_DIR)/limine-uefi-cd.bin
LIMINE_FETCH_SCRIPT := scripts/fetch-limine.sh

LIMINE_PROTOCOL_DIR          := vendor/limine-protocol
LIMINE_HEADER                := $(LIMINE_PROTOCOL_DIR)/limine.h
LIMINE_PROTOCOL_FETCH_SCRIPT := scripts/fetch-limine-protocol.sh

TARGET := x86_64-unknown-none-elf

CFLAGS := \
	--target=$(TARGET) \
	-ffreestanding \
	-fno-stack-protector \
	-fno-common \
	-mno-red-zone \
	-mgeneral-regs-only \
	-mcmodel=kernel \
	-O0 \
	-g \
	-I$(LIMINE_PROTOCOL_DIR) \
	-Wall \
	-Wextra \
	-Werror \
	-Wpedantic

# -----------------------------------------------------------------------------
# Build paths
# -----------------------------------------------------------------------------

BUILD_DIR := build

# -----------------------------------------------------------------------------
# Kernel
# -----------------------------------------------------------------------------

KERNEL_ELF := $(BUILD_DIR)/kernel.elf

KERNEL_OBJS := \
	$(BUILD_DIR)/kernel.o \
	$(BUILD_DIR)/boot.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/framebuffer.o \
	$(BUILD_DIR)/font8x8.o \
	$(BUILD_DIR)/console.o \
	$(BUILD_DIR)/interrupts.o \
	$(BUILD_DIR)/idt.o \
	$(BUILD_DIR)/diagnostics.o \
	$(BUILD_DIR)/runtime.o \
	$(BUILD_DIR)/format.o \
	$(BUILD_DIR)/paging.o \
	$(BUILD_DIR)/panic.o \
	$(BUILD_DIR)/display.o \
	$(BUILD_DIR)/arch.o \
	$(BUILD_DIR)/memory.o \
	$(BUILD_DIR)/process_memory.o \
	$(BUILD_DIR)/process_stack.o \
	$(BUILD_DIR)/process_layout.o \
	$(BUILD_DIR)/gdt.o \
    $(BUILD_DIR)/gdt_asm.o \
    $(BUILD_DIR)/rtc.o \
    $(BUILD_DIR)/usermode.o \
    $(BUILD_DIR)/usermode_asm.o \
    $(BUILD_DIR)/syscall.o \
    $(BUILD_DIR)/process.o \
    $(BUILD_DIR)/scheduler.o \
    $(BUILD_DIR)/pic.o \
    $(BUILD_DIR)/timer.o \
    $(BUILD_DIR)/lapic.o \
    $(BUILD_DIR)/ioapic.o \
    $(BUILD_DIR)/interrupt_topology.o \
    $(BUILD_DIR)/process-programs.o \

LINKER_SCRIPT := kernel/linker.ld

LDFLAGS := \
	-T $(LINKER_SCRIPT)

.PHONY: all

all: $(KERNEL_ELF)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/kernel.o: \
	kernel/kernel.c \
	kernel/arch/x86_64/arch.h \
	kernel/arch/x86_64/paging.h \
	kernel/arch/x86_64/serial.h \
	kernel/boot/boot.h \
	kernel/console/console.h \
	kernel/core/panic.h \
	kernel/drivers/framebuffer.h \
	kernel/memory/memory.h \
	kernel/diagnostics/diagnostics.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: \
	kernel/arch/x86_64/serial.c \
	kernel/arch/x86_64/serial.h \
	kernel/arch/x86_64/io.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/framebuffer.o: \
	kernel/drivers/framebuffer.c \
	kernel/drivers/framebuffer.h \
	kernel/font/font8x8.h \
	$(LIMINE_HEADER) | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/font8x8.o: \
	kernel/font/font8x8.c \
	kernel/font/font8x8.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/console.o: \
	kernel/console/console.c \
	kernel/console/console.h \
	kernel/drivers/framebuffer.h \
	kernel/font/font8x8.h \
	$(LIMINE_HEADER) | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts.o: \
	kernel/arch/x86_64/interrupts.S | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: \
	kernel/arch/x86_64/idt.c \
	kernel/arch/x86_64/idt.h \
	kernel/arch/x86_64/serial.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/diagnostics.o: \
	kernel/diagnostics/diagnostics.c \
	kernel/diagnostics/diagnostics.h \
	kernel/console/console.h \
	kernel/arch/x86_64/serial.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/runtime.o: \
	kernel/diagnostics/runtime.c \
	kernel/diagnostics/runtime.h \
	kernel/core/panic.h \
	kernel/arch/x86_64/paging.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/format.o: \
	kernel/format/format.c \
	kernel/format/format.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: \
	kernel/arch/x86_64/paging.c \
	kernel/arch/x86_64/paging.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/panic.o: \
	kernel/core/panic.c \
	kernel/core/panic.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot.o: \
	kernel/boot/limine.c \
	kernel/boot/boot.h \
	kernel/core/panic.h \
	$(LIMINE_HEADER) | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/display.o: \
	kernel/init/display.c \
	kernel/init/display.h \
	kernel/console/console.h \
	kernel/drivers/framebuffer.h \
	kernel/diagnostics/diagnostics.h \
	kernel/core/panic.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/arch.o: \
	kernel/arch/x86_64/arch.c \
	kernel/arch/x86_64/arch.h \
	kernel/arch/x86_64/idt.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/memory.o: \
	kernel/memory/memory.c \
	kernel/memory/memory.h \
	kernel/core/panic.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process_memory.o: \
	kernel/process/memory.c \
	kernel/process/memory.h \
	kernel/arch/x86_64/paging.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process_stack.o: \
	kernel/process/stack.c \
	kernel/process/stack.h \
	kernel/process/memory.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process_layout.o: \
	kernel/process/layout.c \
	kernel/process/layout.h \
	kernel/process/memory.h \
	kernel/process/stack.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: \
	kernel/arch/x86_64/gdt.c \
	kernel/arch/x86_64/gdt.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt_asm.o: \
	kernel/arch/x86_64/gdt.S | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rtc.o: \
	kernel/arch/x86_64/rtc.c \
	kernel/arch/x86_64/rtc.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usermode.o: \
	kernel/arch/x86_64/usermode.c \
	kernel/arch/x86_64/usermode.h \
	kernel/arch/x86_64/gdt.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usermode_asm.o: \
	kernel/arch/x86_64/usermode.S | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall.o: \
	kernel/syscall/syscall.c \
	kernel/syscall/syscall.h \
	kernel/diagnostics/diagnostics.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process.o: \
	kernel/process/process.c \
	kernel/process/process.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/scheduler.o: \
	kernel/scheduler/scheduler.c \
	kernel/scheduler/scheduler.h \
	kernel/process/process.h \
	kernel/process/memory.h \
	kernel/process/layout.h \
	kernel/arch/x86_64/paging.h \
	kernel/arch/x86_64/usermode.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pic.o: \
	kernel/arch/x86_64/pic.c \
	kernel/arch/x86_64/pic.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/timer.o: \
	kernel/arch/x86_64/timer.c \
	kernel/arch/x86_64/timer.h \
	kernel/arch/x86_64/pic.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lapic.o: \
	kernel/arch/x86_64/lapic.c \
	kernel/arch/x86_64/lapic.h \
	kernel/memory/memory.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ioapic.o: \
	kernel/arch/x86_64/ioapic.c \
	kernel/arch/x86_64/ioapic.h \
	kernel/memory/memory.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupt_topology.o: \
	kernel/arch/x86_64/interrupt_topology.c \
	kernel/arch/x86_64/interrupt_topology.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process-programs.o: \
	kernel/process/programs.c \
	kernel/process/programs.h | $(BUILD_DIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS) $(LINKER_SCRIPT)
	$(LD_LLD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

# -----------------------------------------------------------------------------
# Limine
# -----------------------------------------------------------------------------

.PHONY: limine

limine: $(LIMINE_EFI) $(LIMINE_UEFI_CD)

$(LIMINE_EFI) $(LIMINE_UEFI_CD): $(LIMINE_FETCH_SCRIPT)
	./$(LIMINE_FETCH_SCRIPT)

.PHONY: limine-protocol

limine-protocol: $(LIMINE_HEADER)

$(LIMINE_HEADER): $(LIMINE_PROTOCOL_FETCH_SCRIPT)
	./$(LIMINE_PROTOCOL_FETCH_SCRIPT)

# -----------------------------------------------------------------------------
# ISO image
# -----------------------------------------------------------------------------

ISO_ROOT          := $(BUILD_DIR)/iso-root
ISO_IMAGE         := $(BUILD_DIR)/myos.iso
ISO_KERNEL        := $(ISO_ROOT)/boot/kernel.elf
ISO_LIMINE_CONF   := $(ISO_ROOT)/limine.conf
ISO_BOOTX64       := $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
ISO_LIMINE_UEFI_CD := $(ISO_ROOT)/limine-uefi-cd.bin

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

# -----------------------------------------------------------------------------
# QEMU
# -----------------------------------------------------------------------------

QEMU_FIRMWARE := $(QEMU_PREFIX)/share/qemu/edk2-x86_64-code.fd
QEMU_DEBUG_PID := $(BUILD_DIR)/qemu-debug.pid

.PHONY: run debug debug-stop

run: $(ISO_IMAGE)
	$(QEMU) \
		-machine q35 \
		-cpu qemu64 \
		-m 512M \
		-smp 1 \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) \
		-cdrom $(ISO_IMAGE) \
		-boot d \
		-vga none \
        -device VGA,edid=on,xres=1920,yres=1200 \
        -display cocoa,show-cursor=on \
		-no-reboot \
		-no-shutdown \
		-serial stdio

debug: $(ISO_IMAGE)
	@rm -f $(QEMU_DEBUG_PID)
	$(QEMU) \
		-machine q35 \
		-cpu qemu64 \
		-m 512M \
		-smp 1 \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) \
		-cdrom $(ISO_IMAGE) \
		-boot d \
		-vga none \
		-device VGA,edid=on,xres=1920,yres=1200 \
		-display cocoa,show-cursor=on \
		-no-reboot \
		-no-shutdown \
		-serial stdio \
		-pidfile $(QEMU_DEBUG_PID) \
		-S \
		-gdb tcp::1234

debug-stop:
	@if [ -f "$(QEMU_DEBUG_PID)" ]; then \
		pid="$$(cat "$(QEMU_DEBUG_PID)")"; \
		if kill -0 "$$pid" 2>/dev/null; then \
			echo "Stopping QEMU debug VM (PID $$pid)..."; \
			kill "$$pid"; \
		fi; \
		rm -f "$(QEMU_DEBUG_PID)"; \
	fi

# -----------------------------------------------------------------------------
# Snapshot of current repository status
# -----------------------------------------------------------------------------

.PHONY: snapshot

PROJECT_NAME := $(notdir $(CURDIR))
SNAPSHOT_TIMESTAMP := $(shell date +%Y%m%d-%H%M%S)
SNAPSHOT_FILE := ../$(PROJECT_NAME)-snapshot-$(SNAPSHOT_TIMESTAMP).tar.gz

snapshot:
	@echo "Creating project snapshot..."
	@tar \
		--exclude='$(PROJECT_NAME)/.git' \
		--exclude='$(PROJECT_NAME)/build' \
		--exclude='$(PROJECT_NAME)/vendor' \
		-czf "$(SNAPSHOT_FILE)" \
		-C .. "$(PROJECT_NAME)"
	@echo
	@echo "Snapshot created:"
	@echo "  $(SNAPSHOT_FILE)"
	@du -h "$(SNAPSHOT_FILE)"

# -----------------------------------------------------------------------------
# Toolchain validation
# -----------------------------------------------------------------------------

.PHONY: check-toolchain

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

# -----------------------------------------------------------------------------
# Compdb
# -----------------------------------------------------------------------------

COMPDB := $(BUILD_DIR)/compile_commands.json
COMPDB_TMP := .compile_commands.json.tmp

.PHONY: compdb

compdb:
	@command -v $(BEAR) >/dev/null || { \
		echo "error: Bear is not installed"; \
		exit 1; \
	}
	@rm -f $(COMPDB_TMP)
	@$(MAKE) clean
	@mkdir -p $(BUILD_DIR)
	PATH="$(LLVM_PREFIX)/bin:$$PATH" \
		$(BEAR) -o $(COMPDB_TMP) -- \
		$(MAKE) CLANG=clang all
	@mv $(COMPDB_TMP) $(COMPDB)
	@echo
	@echo "Compilation database created:"
	@echo "  $(COMPDB)"

# -----------------------------------------------------------------------------
# Cleanup
# -----------------------------------------------------------------------------

.PHONY: clean

clean:
	rm -rf $(BUILD_DIR)/*

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

include config.mk

# -----------------------------------------------------------------------------
# Toolchain
# -----------------------------------------------------------------------------

HOST_OS := $(shell uname -s)

MYOS_LLVM_MAJOR := 21

ifeq ($(HOST_OS),Darwin)

LLVM_PREFIX ?= $(shell brew --prefix llvm@21 2>/dev/null)
LLD_PREFIX  ?= $(shell brew --prefix lld@21 2>/dev/null)
QEMU_PREFIX ?= $(shell brew --prefix qemu 2>/dev/null)

CLANG        ?= $(LLVM_PREFIX)/bin/clang
LD_LLD       ?= $(LLD_PREFIX)/bin/ld.lld
LLVM_READELF ?= $(LLVM_PREFIX)/bin/llvm-readelf
LLVM_OBJDUMP ?= $(LLVM_PREFIX)/bin/llvm-objdump
LLVM_NM      ?= $(LLVM_PREFIX)/bin/llvm-nm

QEMU          ?= $(QEMU_PREFIX)/bin/qemu-system-x86_64
QEMU_FIRMWARE ?= $(QEMU_PREFIX)/share/qemu/edk2-x86_64-code.fd
QEMU_DISPLAY  ?= cocoa,show-cursor=on

else ifeq ($(HOST_OS),Linux)

#
# Prefer explicitly versioned LLVM 21 tools when the distribution
# provides them. Fall back to the unversioned system commands.
#
CLANG ?= $(shell \
	command -v clang-$(MYOS_LLVM_MAJOR) 2>/dev/null || \
	command -v clang 2>/dev/null \
)

LD_LLD ?= $(shell \
	command -v ld.lld-$(MYOS_LLVM_MAJOR) 2>/dev/null || \
	command -v ld.lld 2>/dev/null \
)

LLVM_READELF ?= $(shell \
	command -v llvm-readelf-$(MYOS_LLVM_MAJOR) 2>/dev/null || \
	command -v llvm-readelf 2>/dev/null \
)

LLVM_OBJDUMP ?= $(shell \
	command -v llvm-objdump-$(MYOS_LLVM_MAJOR) 2>/dev/null || \
	command -v llvm-objdump 2>/dev/null \
)

LLVM_NM ?= $(shell \
	command -v llvm-nm-$(MYOS_LLVM_MAJOR) 2>/dev/null || \
	command -v llvm-nm 2>/dev/null \
)

QEMU ?= $(shell command -v qemu-system-x86_64 2>/dev/null)

#
# OVMF location varies between distributions/packages.
# Use the first firmware image that actually exists.
#
QEMU_FIRMWARE ?= $(firstword \
	$(wildcard /usr/share/edk2/ovmf/OVMF_CODE.fd) \
	$(wildcard /usr/share/edk2/x64/OVMF_CODE.4m.fd) \
	$(wildcard /usr/share/OVMF/OVMF_CODE.fd) \
	$(wildcard /usr/share/OVMF/OVMF_CODE_4M.fd) \
)

QEMU_DISPLAY ?= gtk,show-cursor=on

else

$(error Unsupported host operating system: $(HOST_OS))

endif

QEMU_TEST_MEMORY ?= 512M

GDB     ?= gdb
XORRISO ?= xorriso
BEAR    ?= bear

SGDISK        ?= sgdisk
MTOOLS_FORMAT ?= mformat
MTOOLS_MKDIR  ?= mmd
MTOOLS_COPY   ?= mcopy

QEMU_TEST_EXIT_PORT          := 0xF4
QEMU_TEST_EXIT_SUCCESS_VALUE := 0x10
QEMU_TEST_EXIT_FAILURE_VALUE := 0x11

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
MYOS_INCLUDE_DIR := include

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
	-I$(MYOS_INCLUDE_DIR) \
	-DMYOS_RUNTIME_DIAGNOSTICS=$(MYOS_RUNTIME_DIAGNOSTICS) \
	-DMYOS_KERNEL_TESTS=$(MYOS_KERNEL_TESTS) \
	-DMYOS_QEMU_TEST_EXIT=$(MYOS_QEMU_TEST_EXIT) \
	-DMYOS_QEMU_TEST_EXIT_PORT=$(QEMU_TEST_EXIT_PORT) \
	-DMYOS_QEMU_TEST_EXIT_SUCCESS_VALUE=$(QEMU_TEST_EXIT_SUCCESS_VALUE) \
	-DMYOS_QEMU_TEST_EXIT_FAILURE_VALUE=$(QEMU_TEST_EXIT_FAILURE_VALUE) \
	-Wall \
	-Wextra \
	-Werror \
	-Wpedantic

# -----------------------------------------------------------------------------
# Build paths
# -----------------------------------------------------------------------------

BUILD_DIR := build
CONFIG_STAMP := $(BUILD_DIR)/config.stamp

# -----------------------------------------------------------------------------
# Bootstrap userspace
# -----------------------------------------------------------------------------

include mk/userspace-runtime.mk

# -----------------------------------------------------------------------------
# Userspace test images
# -----------------------------------------------------------------------------

include mk/userspace-tests.mk

# -----------------------------------------------------------------------------
# Initramfs
# -----------------------------------------------------------------------------

include mk/initramfs.mk

# -----------------------------------------------------------------------------
# Kernel
# -----------------------------------------------------------------------------

include mk/kernel.mk

.PHONY: all

all: $(KERNEL_ELF)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

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

ISO_ROOT           := $(BUILD_DIR)/iso-root
ISO_IMAGE          := $(BUILD_DIR)/myos.iso
ISO_KERNEL         := $(ISO_ROOT)/boot/kernel.elf
ISO_LIMINE_CONF    := $(ISO_ROOT)/limine.conf
ISO_BOOTX64        := $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
ISO_LIMINE_UEFI_CD := $(ISO_ROOT)/limine-uefi-cd.bin
ISO_INITRAMFS := $(ISO_ROOT)/boot/initramfs.cpio

TEST_ISO_ROOT           := $(BUILD_DIR)/iso-test-root
TEST_ISO_IMAGE          := $(BUILD_DIR)/myos-test.iso
TEST_ISO_KERNEL         := $(TEST_ISO_ROOT)/boot/kernel.elf
TEST_ISO_LIMINE_CONF    := $(TEST_ISO_ROOT)/limine.conf
TEST_ISO_BOOTX64        := $(TEST_ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
TEST_ISO_LIMINE_UEFI_CD := $(TEST_ISO_ROOT)/limine-uefi-cd.bin
TEST_ISO_INITRAMFS := $(TEST_ISO_ROOT)/boot/initramfs.cpio

.PHONY: iso iso-tests

iso: $(ISO_IMAGE)

iso-tests:
	$(MAKE) \
		MYOS_KERNEL_TESTS=1 \
		MYOS_RUNTIME_DIAGNOSTICS=0 \
		MYOS_QEMU_TEST_EXIT=0 \
		$(TEST_ISO_IMAGE)

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
	$(ISO_LIMINE_UEFI_CD) \
	$(ISO_INITRAMFS)
	$(XORRISO) \
		-as mkisofs \
		-R -r -J \
		-b limine-uefi-cd.bin \
		-no-emul-boot \
		-o $(ISO_IMAGE) \
		$(ISO_ROOT)

$(TEST_ISO_ROOT):
	mkdir -p $(TEST_ISO_ROOT)/boot
	mkdir -p $(TEST_ISO_ROOT)/EFI/BOOT

$(TEST_ISO_KERNEL): $(KERNEL_ELF) | $(TEST_ISO_ROOT)
	cp $(KERNEL_ELF) $(TEST_ISO_KERNEL)

$(TEST_ISO_LIMINE_CONF): limine-test.conf | $(TEST_ISO_ROOT)
	cp limine-test.conf $(TEST_ISO_LIMINE_CONF)

$(TEST_ISO_BOOTX64): $(LIMINE_EFI) | $(TEST_ISO_ROOT)
	cp $(LIMINE_EFI) $(TEST_ISO_BOOTX64)

$(TEST_ISO_LIMINE_UEFI_CD): $(LIMINE_UEFI_CD) | $(TEST_ISO_ROOT)
	cp $(LIMINE_UEFI_CD) $(TEST_ISO_LIMINE_UEFI_CD)

$(TEST_ISO_IMAGE): \
	$(TEST_ISO_KERNEL) \
	$(TEST_ISO_LIMINE_CONF) \
	$(TEST_ISO_BOOTX64) \
	$(TEST_ISO_LIMINE_UEFI_CD) \
	$(TEST_ISO_INITRAMFS)
	$(XORRISO) \
		-as mkisofs \
		-R -r -J \
		-b limine-uefi-cd.bin \
		-no-emul-boot \
		-o $(TEST_ISO_IMAGE) \
		$(TEST_ISO_ROOT)

$(ISO_INITRAMFS): $(INITRAMFS_IMAGE) | $(ISO_ROOT)
	cp $(INITRAMFS_IMAGE) $(ISO_INITRAMFS)

$(TEST_ISO_INITRAMFS): $(TEST_INITRAMFS_IMAGE) | $(TEST_ISO_ROOT)
	cp $(TEST_INITRAMFS_IMAGE) $(TEST_ISO_INITRAMFS)

# -----------------------------------------------------------------------------
# USB boot image
# -----------------------------------------------------------------------------

USB_IMAGE         	:= $(BUILD_DIR)/myos-usb.img

USB_IMAGE_SIZE_MIB := 64

USB_HEADS := 64
USB_SECTORS_PER_TRACK := 32

USB_CYLINDER_SECTORS := $(shell \
	echo $$(( $(USB_HEADS) * $(USB_SECTORS_PER_TRACK) )) \
)

USB_PART_START := $(USB_CYLINDER_SECTORS)

USB_PART_SECTORS := $(shell \
	echo $$(( ($(USB_IMAGE_SIZE_MIB) - 2) * $(USB_CYLINDER_SECTORS) )) \
)

USB_PART_END := $(shell \
	echo $$(( $(USB_PART_START) + $(USB_PART_SECTORS) - 1 )) \
)

USB_PART_OFFSET := $(shell \
	echo $$(( $(USB_PART_START) * 512 )) \
)

.PHONY: usb-image check-usb-tools

usb-image: $(USB_IMAGE)

check-usb-tools:
	@command -v $(SGDISK) >/dev/null 2>&1 || { \
		echo "ERROR: sgdisk not found (install gptfdisk)"; \
		exit 1; \
	}
	@command -v $(MTOOLS_FORMAT) >/dev/null 2>&1 || { \
		echo "ERROR: mformat not found (install mtools)"; \
		exit 1; \
	}
	@command -v $(MTOOLS_MKDIR) >/dev/null 2>&1 || { \
		echo "ERROR: mmd not found (install mtools)"; \
		exit 1; \
	}
	@command -v $(MTOOLS_COPY) >/dev/null 2>&1 || { \
		echo "ERROR: mcopy not found (install mtools)"; \
		exit 1; \
	}

$(USB_IMAGE): \
	$(KERNEL_ELF) \
	$(INITRAMFS_IMAGE) \
	limine.conf \
	$(LIMINE_EFI) | check-usb-tools
	@echo "Creating bootable UEFI USB image..."
	rm -f $@
	dd if=/dev/zero of=$@ bs=1048576 count=$(USB_IMAGE_SIZE_MIB)
	$(SGDISK) \
		-n 1:$(USB_PART_START):$(USB_PART_END) \
		-t 1:ef00 \
		$@

	$(MTOOLS_FORMAT) \
		-i $@@@$(USB_PART_OFFSET) \
		-T $(USB_PART_SECTORS) \
		-h $(USB_HEADS) \
		-s $(USB_SECTORS_PER_TRACK) \
		::
	$(MTOOLS_MKDIR) -i $@@@$(USB_PART_OFFSET) \
		::/EFI \
		::/EFI/BOOT \
		::/boot

	$(MTOOLS_COPY) -i $@@@$(USB_PART_OFFSET) \
		$(LIMINE_EFI) \
		::/EFI/BOOT/BOOTX64.EFI

	$(MTOOLS_COPY) -i $@@@$(USB_PART_OFFSET) \
		$(KERNEL_ELF) \
		::/boot/kernel.elf

	$(MTOOLS_COPY) -i $@@@$(USB_PART_OFFSET) \
		$(INITRAMFS_IMAGE) \
		::/boot/initramfs.cpio

	$(MTOOLS_COPY) -i $@@@$(USB_PART_OFFSET) \
		limine.conf \
		::/limine.conf
	@echo
	@echo "Bootable USB image created:"
	@echo "  $(USB_IMAGE)"

.PHONY: usb-image-tests usb-image-diagnostics

usb-image-tests:
	$(MAKE) \
		MYOS_KERNEL_TESTS=1 \
		usb-image

usb-image-diagnostics:
	$(MAKE) \
		MYOS_RUNTIME_DIAGNOSTICS=1 \
		MYOS_KERNEL_TESTS=1 \
		usb-image

# -----------------------------------------------------------------------------
# QEMU
# -----------------------------------------------------------------------------

QEMU_DEBUG_PID := $(BUILD_DIR)/qemu-debug.pid
QEMU_DISPLAY_RESOLUTION := "xres=1280,yres=1024"

# Keep firmware serial output from resizing the host terminal.
QEMU_SERIAL_RUNNER := python3 scripts/qemu-serial-console.py

.PHONY: run run-with-tests run-usb run-usb-tests run-usb-diagnostics debug debug-stop run-qemu-tests

run: $(ISO_IMAGE)
	$(QEMU_SERIAL_RUNNER) $(QEMU) \
		-machine q35 \
		-cpu qemu64 \
		-m 512M \
		-smp 1 \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) \
		-cdrom $(ISO_IMAGE) \
		-boot d \
		-vga none \
		-device VGA,edid=on,$(QEMU_DISPLAY_RESOLUTION) \
		-display $(QEMU_DISPLAY) \
		-no-reboot \
		-no-shutdown \
		-serial stdio

run-with-tests:
	$(MAKE) \
		MYOS_KERNEL_TESTS=1 \
		MYOS_RUNTIME_DIAGNOSTICS=0 \
		MYOS_QEMU_TEST_EXIT=0 \
		run

run-qemu-tests: $(TEST_ISO_IMAGE)
	@set +e; \
	$(QEMU_SERIAL_RUNNER) $(QEMU) \
		-machine q35 \
		-cpu qemu64 \
		-m $(QEMU_TEST_MEMORY) \
		-smp 1 \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) \
		-cdrom $(TEST_ISO_IMAGE) \
		-boot d \
		-vga none \
		-device VGA,edid=on,$(QEMU_DISPLAY_RESOLUTION) \
		-display none \
		-monitor none \
		-no-reboot \
		-serial stdio \
		-device isa-debug-exit,iobase=$(QEMU_TEST_EXIT_PORT),iosize=0x04; \
	status=$$?; \
	success_status=$$(( ($(QEMU_TEST_EXIT_SUCCESS_VALUE) << 1) | 1 )); \
	failure_status=$$(( ($(QEMU_TEST_EXIT_FAILURE_VALUE) << 1) | 1 )); \
	echo; \
	if [ "$$status" -eq "$$success_status" ]; then \
		echo "[test] QEMU kernel tests passed"; \
		exit 0; \
	fi; \
	if [ "$$status" -eq "$$failure_status" ]; then \
		echo "[test] QEMU kernel tests failed"; \
		exit 1; \
	fi; \
	echo "[test] QEMU exited unexpectedly with status $$status"; \
	exit 1

run-usb: $(USB_IMAGE)
	$(QEMU_SERIAL_RUNNER) $(QEMU) \
		-machine q35 \
		-cpu qemu64 \
		-m 512M \
		-smp 1 \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) \
		-device qemu-xhci,id=xhci \
		-drive if=none,format=raw,readonly=on,file=$(USB_IMAGE),id=myos-usb \
		-device usb-storage,bus=xhci.0,drive=myos-usb,bootindex=1 \
		-vga none \
		-device VGA,edid=on,$(QEMU_DISPLAY_RESOLUTION) \
		-display $(QEMU_DISPLAY) \
		-no-reboot \
		-no-shutdown \
		-serial stdio

run-usb-tests:
	$(MAKE) \
		MYOS_KERNEL_TESTS=1 \
		run-usb

run-usb-diagnostics:
	$(MAKE) \
		MYOS_RUNTIME_DIAGNOSTICS=1 \
		MYOS_KERNEL_TESTS=1 \
		run-usb

debug: $(ISO_IMAGE)
	@rm -f $(QEMU_DEBUG_PID)
	$(QEMU_SERIAL_RUNNER) $(QEMU) \
		-machine q35 \
		-cpu qemu64 \
		-m 512M \
		-smp 1 \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) \
		-cdrom $(ISO_IMAGE) \
		-boot d \
		-vga none \
		-device VGA,edid=on,$(QEMU_DISPLAY_RESOLUTION) \
		-display $(QEMU_DISPLAY) \
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
# Development configurations
# -----------------------------------------------------------------------------

.PHONY: run-tests test-qemu debug-tests run-diagnostics debug-diagnostics

run-tests:
	$(MAKE) \
		MYOS_KERNEL_TESTS=1 \
		MYOS_RUNTIME_DIAGNOSTICS=0 \
		MYOS_QEMU_TEST_EXIT=1 \
		run-qemu-tests

test-qemu:
	$(MAKE) \
		MYOS_KERNEL_TESTS=1 \
		MYOS_RUNTIME_DIAGNOSTICS=0 \
		MYOS_QEMU_TEST_EXIT=1 \
		run-qemu-tests

debug-tests:
	$(MAKE) \
		MYOS_KERNEL_TESTS=1 \
		MYOS_RUNTIME_DIAGNOSTICS=0 \
		debug

run-diagnostics:
	$(MAKE) \
		MYOS_KERNEL_TESTS=1 \
		MYOS_RUNTIME_DIAGNOSTICS=1 \
		MYOS_QEMU_TEST_EXIT=1 \
		run-qemu-tests

debug-diagnostics:
	$(MAKE) \
		MYOS_KERNEL_TESTS=1 \
		MYOS_RUNTIME_DIAGNOSTICS=1 \
		debug

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
	@echo "Host OS: $(HOST_OS)"
	@test -x "$(CLANG)" || { echo "ERROR: Clang not found at $(CLANG)"; exit 1; }
	@test -x "$(LD_LLD)" || { echo "ERROR: LLD not found at $(LD_LLD)"; exit 1; }
	@test -x "$(LLVM_READELF)" || { echo "ERROR: llvm-readelf not found"; exit 1; }
	@test -x "$(LLVM_OBJDUMP)" || { echo "ERROR: llvm-objdump not found"; exit 1; }
	@test -x "$(LLVM_NM)" || { echo "ERROR: llvm-nm not found"; exit 1; }
	@test -f "$(QEMU_FIRMWARE)" || { echo "ERROR: QEMU firmware not found at $(QEMU_FIRMWARE)"; exit 1; }
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
	@echo "-- QEMU firmware --"
	@echo "$(QEMU_FIRMWARE)"
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

.PHONY: compdb

compdb:
	@rm -f $(COMPDB)
	@if [ -d "$(KERNEL_OBJ_DIR)" ]; then \
		find "$(KERNEL_OBJ_DIR)" \
			-type f \
			-name '*.o.json' \
			-delete; \
	fi
	@$(MAKE) -B \
		COMPDB_CAPTURE=1 \
		MYOS_RUNTIME_DIAGNOSTICS=1 \
		MYOS_KERNEL_TESTS=1 \
		MYOS_QEMU_TEST_EXIT=1 \
		all
	@{ \
		printf '[\n'; \
		first=1; \
		for file in $$(find "$(KERNEL_OBJ_DIR)/c" \
			-type f \
			-name '*.o.json' \
			| sort); do \
			if [ $$first -eq 0 ]; then \
				printf ',\n'; \
			fi; \
			sed '$$s/,$$//' "$$file"; \
			first=0; \
		done; \
		printf '\n]\n'; \
	} > $(COMPDB)
	@echo
	@echo "Compilation database created:"
	@echo "  $(COMPDB)"

# -----------------------------------------------------------------------------
# Cleanup
# -----------------------------------------------------------------------------

.PHONY: clean

clean:
	rm -rf $(BUILD_DIR)/*

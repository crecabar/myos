.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

include config.mk

# -----------------------------------------------------------------------------
# Toolchain
# -----------------------------------------------------------------------------

HOST_OS := $(shell uname -s)

ifeq ($(HOST_OS),Darwin)
LLVM_PREFIX  ?= $(shell brew --prefix llvm@21 2>/dev/null)
LLD_PREFIX   ?= $(shell brew --prefix lld@21 2>/dev/null)
QEMU_PREFIX  ?= $(shell brew --prefix qemu 2>/dev/null)
QEMU_FIRMWARE ?= $(QEMU_PREFIX)/share/qemu/edk2-x86_64-code.fd
QEMU_DISPLAY  ?= cocoa,show-cursor=on
else ifeq ($(HOST_OS),Linux)
LLVM_PREFIX  ?= /usr/lib/llvm21
LLD_PREFIX   ?= /usr/lib/llvm21
QEMU_PREFIX  ?= /usr
QEMU_FIRMWARE ?= /usr/share/edk2/x64/OVMF_CODE.4m.fd
QEMU_DISPLAY  ?= gtk,show-cursor=on
else
$(error Unsupported host operating system: $(HOST_OS))
endif

CLANG        ?= $(LLVM_PREFIX)/bin/clang
LD_LLD       ?= $(LLD_PREFIX)/bin/ld.lld
LLVM_READELF ?= $(LLVM_PREFIX)/bin/llvm-readelf
LLVM_OBJDUMP ?= $(LLVM_PREFIX)/bin/llvm-objdump
LLVM_NM      ?= $(LLVM_PREFIX)/bin/llvm-nm

QEMU    ?= qemu-system-x86_64
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
# Userspace test images
# -----------------------------------------------------------------------------

USER_TEST_BUILD_DIR := $(BUILD_DIR)/user-tests

ELF_ENTRY_SOURCE        := user/tests/elf_entry.S
ELF_ENTRY_LINKER_SCRIPT := user/tests/elf_entry.ld
ELF_ENTRY_OBJECT        := $(USER_TEST_BUILD_DIR)/elf_entry.o
ELF_ENTRY_ELF           := $(USER_TEST_BUILD_DIR)/elf_entry.elf

ELF_ENTRY_CFLAGS := \
	--target=$(TARGET) \
	-ffreestanding \
	-fno-stack-protector \
	-fno-common \
	-mno-red-zone \
	-mgeneral-regs-only \
	-O0 \
	-g

$(USER_TEST_BUILD_DIR):
	mkdir -p $(USER_TEST_BUILD_DIR)

$(ELF_ENTRY_OBJECT): $(ELF_ENTRY_SOURCE) | $(USER_TEST_BUILD_DIR)
	$(CLANG) $(ELF_ENTRY_CFLAGS) \
		-c $< \
		-o $@

$(ELF_ENTRY_ELF): $(ELF_ENTRY_OBJECT) $(ELF_ENTRY_LINKER_SCRIPT)
	$(LD_LLD) \
		-T $(ELF_ENTRY_LINKER_SCRIPT) \
		-o $@ \
		$(ELF_ENTRY_OBJECT)

# -----------------------------------------------------------------------------
# Kernel
# -----------------------------------------------------------------------------

KERNEL_ELF := $(BUILD_DIR)/kernel.elf

# Kernel production sources. Files below tests/ are intentionally excluded.
KERNEL_PRODUCTION_C_SOURCES := $(shell \
	find kernel \
		-type f \
		-name '*.c' \
		! -path '*/tests/*' \
		-print | sort \
)

KERNEL_PRODUCTION_ASM_SOURCES := $(shell \
	find kernel \
		-type f \
		-name '*.S' \
		! -path '*/tests/*' \
		-print | sort \
)

# Kernel test sources. runtime_diagnostics.c is controlled independently.
KERNEL_TEST_C_SOURCES := $(shell \
	find kernel \
		-type f \
		-name '*.c' \
		-path '*/tests/*' \
		! -name 'runtime_diagnostics.c' \
		-print | sort \
)

KERNEL_TEST_ASM_SOURCES := $(shell \
	find kernel \
		-type f \
		-name '*.S' \
		-path '*/tests/*' \
		-print | sort \
)

# Sources included in the current kernel build configuration.
KERNEL_C_SOURCES := $(KERNEL_PRODUCTION_C_SOURCES)
KERNEL_ASM_SOURCES := $(KERNEL_PRODUCTION_ASM_SOURCES)

ifeq ($(MYOS_RUNTIME_DIAGNOSTICS),1)
	KERNEL_C_SOURCES += kernel/tests/runtime_diagnostics.c
endif

ifeq ($(MYOS_KERNEL_TESTS),1)
	KERNEL_C_SOURCES += $(KERNEL_TEST_C_SOURCES)
	KERNEL_ASM_SOURCES += $(KERNEL_TEST_ASM_SOURCES)
endif

KERNEL_OBJ_DIR := $(BUILD_DIR)/obj

KERNEL_C_OBJS := \
	$(patsubst %.c,$(KERNEL_OBJ_DIR)/c/%.o,$(KERNEL_C_SOURCES))

KERNEL_ASM_OBJS := \
	$(patsubst %.S,$(KERNEL_OBJ_DIR)/asm/%.o,$(KERNEL_ASM_SOURCES))

PROCESS_ELF_ENTRY_FIXTURE_OBJ := \
	$(KERNEL_OBJ_DIR)/asm/kernel/tests/process_elf_entry_fixture.o

KERNEL_OBJS := \
	$(KERNEL_C_OBJS) \
	$(KERNEL_ASM_OBJS)

KERNEL_DEPS := \
	$(KERNEL_C_OBJS:.o=.d) \
	$(KERNEL_ASM_OBJS:.o=.d)

LINKER_SCRIPT := kernel/linker.ld

LDFLAGS := \
	-T $(LINKER_SCRIPT)

.PHONY: kernel-sources

kernel-sources:
	@echo "=== Kernel production C sources ==="
	@printf '  %s\n' $(KERNEL_PRODUCTION_C_SOURCES)
	@echo
	@echo "=== Kernel production assembly sources ==="
	@printf '  %s\n' $(KERNEL_PRODUCTION_ASM_SOURCES)
	@echo
	@echo "=== Kernel test C sources ==="
	@printf '  %s\n' $(KERNEL_TEST_C_SOURCES)
	@echo
	@echo "=== Kernel test assembly sources ==="
	@printf '  %s\n' $(KERNEL_TEST_ASM_SOURCES)

.PHONY: kernel-objects

kernel-objects:
	@echo "=== Kernel C objects ==="
	@printf '  %s\n' $(KERNEL_C_OBJS)
	@echo
	@echo "=== Kernel assembly objects ==="
	@printf '  %s\n' $(KERNEL_ASM_OBJS)


.PHONY: all

all: $(KERNEL_ELF)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

.PHONY: FORCE
FORCE:

$(CONFIG_STAMP): FORCE | $(BUILD_DIR)
	@printf '%s\n' \
		'MYOS_RUNTIME_DIAGNOSTICS=$(MYOS_RUNTIME_DIAGNOSTICS)' \
		'MYOS_KERNEL_TESTS=$(MYOS_KERNEL_TESTS)' \
		'MYOS_QEMU_TEST_EXIT=$(MYOS_QEMU_TEST_EXIT)' \
		'QEMU_TEST_EXIT_PORT=$(QEMU_TEST_EXIT_PORT)' \
		'QEMU_TEST_EXIT_SUCCESS_VALUE=$(QEMU_TEST_EXIT_SUCCESS_VALUE)' \
		'QEMU_TEST_EXIT_FAILURE_VALUE=$(QEMU_TEST_EXIT_FAILURE_VALUE)' \
		> $(CONFIG_STAMP).tmp
	@if ! cmp -s $(CONFIG_STAMP).tmp $(CONFIG_STAMP); then \
		mv $(CONFIG_STAMP).tmp $(CONFIG_STAMP); \
	else \
		rm -f $(CONFIG_STAMP).tmp; \
	fi

$(KERNEL_OBJ_DIR)/c/%.o: %.c $(CONFIG_STAMP) | $(LIMINE_HEADER)
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) \
		-MMD \
		-MP \
		-MF $(@:.o=.d) \
		-MT $@ \
		-c $< \
		-o $@

$(KERNEL_OBJ_DIR)/asm/%.o: %.S $(CONFIG_STAMP) | $(LIMINE_HEADER)
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) \
		-MMD \
		-MP \
		-MF $(@:.o=.d) \
		-MT $@ \
		-c $< \
		-o $@

ifeq ($(MYOS_KERNEL_TESTS),1)
$(PROCESS_ELF_ENTRY_FIXTURE_OBJ): $(ELF_ENTRY_ELF)
endif

$(KERNEL_ELF): $(KERNEL_OBJS) $(LINKER_SCRIPT)
	$(LD_LLD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

-include $(KERNEL_DEPS)

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

TEST_ISO_ROOT           := $(BUILD_DIR)/iso-test-root
TEST_ISO_IMAGE          := $(BUILD_DIR)/myos-test.iso
TEST_ISO_KERNEL         := $(TEST_ISO_ROOT)/boot/kernel.elf
TEST_ISO_LIMINE_CONF    := $(TEST_ISO_ROOT)/limine.conf
TEST_ISO_BOOTX64        := $(TEST_ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
TEST_ISO_LIMINE_UEFI_CD := $(TEST_ISO_ROOT)/limine-uefi-cd.bin

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
	$(TEST_ISO_LIMINE_UEFI_CD)
	$(XORRISO) \
		-as mkisofs \
		-R -r -J \
		-b limine-uefi-cd.bin \
		-no-emul-boot \
		-o $(TEST_ISO_IMAGE) \
		$(TEST_ISO_ROOT)

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
		limine.conf \
		::/limine.conf
	@echo
	@echo "Bootable USB image created:"
	@echo "  $(USB_IMAGE)"

.PHONY: usb-image-diagnostics

usb-image-diagnostics:
	$(MAKE) \
		MYOS_RUNTIME_DIAGNOSTICS=1 \
		MYOS_KERNEL_TESTS=1 \
		usb-image

# -----------------------------------------------------------------------------
# QEMU
# -----------------------------------------------------------------------------

QEMU_DEBUG_PID := $(BUILD_DIR)/qemu-debug.pid

.PHONY: run run-usb run-usb-diagnostics debug debug-stop run-qemu-tests

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
		-display $(QEMU_DISPLAY) \
		-no-reboot \
		-no-shutdown \
		-serial stdio

run-qemu-tests: $(TEST_ISO_IMAGE)
	@set +e; \
	$(QEMU) \
		-machine q35 \
		-cpu qemu64 \
		-m 512M \
		-smp 1 \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) \
		-cdrom $(TEST_ISO_IMAGE) \
		-boot d \
		-vga none \
		-device VGA,edid=on,xres=1920,yres=1200 \
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
	$(QEMU) \
		-machine q35 \
		-cpu qemu64 \
		-m 512M \
		-smp 1 \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) \
		-device qemu-xhci,id=xhci \
		-drive if=none,format=raw,readonly=on,file=$(USB_IMAGE),id=myos-usb \
		-device usb-storage,bus=xhci.0,drive=myos-usb,bootindex=1 \
		-vga none \
		-device VGA,edid=on,xres=1920,yres=1200 \
		-display $(QEMU_DISPLAY) \
		-no-reboot \
		-no-shutdown \
		-serial stdio

run-usb-diagnostics:
	$(MAKE) \
		MYOS_RUNTIME_DIAGNOSTICS=1 \
		MYOS_KERNEL_TESTS=1 \
		run-usb

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
		run

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
		run

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
	@$(BEAR) -o $(COMPDB_TMP) -- \
		$(MAKE) CLANG="$(CLANG)" all
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

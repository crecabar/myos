# -----------------------------------------------------------------------------
# Base MyOS Makefile
# -----------------------------------------------------------------------------

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

include config.mk
include mk/toolchain.mk



# -----------------------------------------------------------------------------
# Build paths
# -----------------------------------------------------------------------------

BUILD_DIR := build

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
# Boot images
# -----------------------------------------------------------------------------

include mk/boot-images.mk

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

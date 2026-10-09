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

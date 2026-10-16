# -----------------------------------------------------------------------------
# Third-party dependencies
# -----------------------------------------------------------------------------

LIMINE_DIR          := vendor/limine
LIMINE_EFI          := $(LIMINE_DIR)/BOOTX64.EFI
LIMINE_UEFI_CD      := $(LIMINE_DIR)/limine-uefi-cd.bin
LIMINE_FETCH_SCRIPT := scripts/fetch-limine.sh

# -----------------------------------------------------------------------------
# Limine
# -----------------------------------------------------------------------------

.PHONY: limine

limine: $(LIMINE_EFI) $(LIMINE_UEFI_CD)

$(LIMINE_EFI) $(LIMINE_UEFI_CD): $(LIMINE_FETCH_SCRIPT)
	./$(LIMINE_FETCH_SCRIPT)

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

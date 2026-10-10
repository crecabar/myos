# -----------------------------------------------------------------------------
# Initramfs
# -----------------------------------------------------------------------------

INITRAMFS_ROOT         := initramfs/root
INITRAMFS_STAGING_ROOT := $(BUILD_DIR)/initramfs-root
INITRAMFS_BUILDER      := scripts/build-initramfs.py
INITRAMFS_IMAGE        := $(BUILD_DIR)/initramfs.cpio

TEST_INITRAMFS_ROOT  := $(BUILD_DIR)/initramfs-test-root
TEST_INITRAMFS_IMAGE := $(BUILD_DIR)/initramfs-test.cpio

INITRAMFS_SOURCES := $(shell \
	find $(INITRAMFS_ROOT) \
		-type f \
		-print 2>/dev/null | sort \
)

$(INITRAMFS_IMAGE): \
	$(INITRAMFS_BUILDER) \
	$(INITRAMFS_SOURCES) \
	$(USER_INIT_ELF) \
	$(USER_PROGRAM_ELFS) \
	| $(BUILD_DIR)
	rm -rf $(INITRAMFS_STAGING_ROOT)
	mkdir -p $(INITRAMFS_STAGING_ROOT)/bin
	cp -R $(INITRAMFS_ROOT)/. $(INITRAMFS_STAGING_ROOT)/
	cp \
		$(USER_INIT_ELF) \
		$(INITRAMFS_STAGING_ROOT)/init
		@set -e; \
	$(foreach program,$(USER_PROGRAM_NAMES), \
		cp $(USER_PROGRAM_BUILD_DIR)/$(program).elf \
			$(INITRAMFS_STAGING_ROOT)/bin/$(program);)
	python3 \
		$(INITRAMFS_BUILDER) \
		$(INITRAMFS_STAGING_ROOT) \
		$@

$(TEST_INITRAMFS_IMAGE): \
	$(INITRAMFS_BUILDER) \
	$(INITRAMFS_SOURCES) \
	$(USER_INIT_ELF) \
	$(USER_PROGRAM_ELFS) \
	$(ELF_FROM_VFS_ELF) \
	$(EXEC_TARGET_ELF) \
	$(RUNTIME_STRING_ELF) \
	$(RUNTIME_MALLOC_ELF) \
	$(RUNTIME_STDIO_ELF) \
	$(RUNTIME_SYSCALLS_ELF) \
	| $(BUILD_DIR)
	rm -rf $(TEST_INITRAMFS_ROOT)
	mkdir -p $(TEST_INITRAMFS_ROOT)/bin
	cp -R $(INITRAMFS_ROOT)/. $(TEST_INITRAMFS_ROOT)/
	cp \
		$(USER_INIT_ELF) \
		$(TEST_INITRAMFS_ROOT)/init
		@set -e; \
	$(foreach program,$(USER_PROGRAM_NAMES), \
		cp $(USER_PROGRAM_BUILD_DIR)/$(program).elf \
			$(TEST_INITRAMFS_ROOT)/bin/$(program);)
	cp \
		$(ELF_FROM_VFS_ELF) \
		$(TEST_INITRAMFS_ROOT)/bin/elf-from-vfs
	cp \
		$(EXEC_TARGET_ELF) \
		$(TEST_INITRAMFS_ROOT)/bin/exec-target
	cp \
		$(RUNTIME_STRING_ELF) \
		$(TEST_INITRAMFS_ROOT)/bin/runtime-string
	cp \
		$(RUNTIME_MALLOC_ELF) \
		$(TEST_INITRAMFS_ROOT)/bin/runtime-malloc
	cp \
		$(RUNTIME_STDIO_ELF) \
		$(TEST_INITRAMFS_ROOT)/bin/runtime-stdio
	cp \
		$(RUNTIME_SYSCALLS_ELF) \
		$(TEST_INITRAMFS_ROOT)/bin/runtime-syscalls
	python3 \
		$(INITRAMFS_BUILDER) \
		$(TEST_INITRAMFS_ROOT) \
		$@

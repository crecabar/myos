# -----------------------------------------------------------------------------
# Kernel
# -----------------------------------------------------------------------------

KERNEL_ELF := $(BUILD_DIR)/kernel.elf

LIMINE_PROTOCOL_DIR          := vendor/limine-protocol
LIMINE_HEADER                := $(LIMINE_PROTOCOL_DIR)/limine.h
LIMINE_PROTOCOL_FETCH_SCRIPT := scripts/fetch-limine-protocol.sh

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

CONFIG_STAMP := $(BUILD_DIR)/config.stamp

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

PROCESS_SYSCALL_ABI_FIXTURE_OBJ := \
	$(KERNEL_OBJ_DIR)/asm/kernel/tests/process_syscall_abi_fixture.o

PROCESS_SYSCALL_POINTER_FIXTURE_OBJ := \
	$(KERNEL_OBJ_DIR)/asm/kernel/tests/process_syscall_pointer_fixture.o

PROCESS_FD_SYSCALLS_FIXTURE_OBJ := \
	$(KERNEL_OBJ_DIR)/asm/kernel/tests/process_fd_syscalls_fixture.o

PROCESS_SCHEDULER_CONTEXT_FIXTURE_OBJ := \
	$(KERNEL_OBJ_DIR)/asm/kernel/tests/process_scheduler_context_fixture.o

PROCESS_EXEC_FIXTURE_OBJ := \
	$(KERNEL_OBJ_DIR)/asm/kernel/tests/process_exec_fixture.o

PROCESS_WAITPID_FIXTURE_OBJ := \
	$(KERNEL_OBJ_DIR)/asm/kernel/tests/process_waitpid_fixture.o

PROCESS_FORK_FIXTURE_OBJ := \
	$(KERNEL_OBJ_DIR)/asm/kernel/tests/process_fork_fixture.o

PROCESS_ELF_PROTECTION_FIXTURE_OBJ := \
	$(KERNEL_OBJ_DIR)/asm/kernel/tests/process_elf_protection_fixture.o

PROCESS_ELF_ISOLATION_FIXTURE_OBJ := \
	$(KERNEL_OBJ_DIR)/asm/kernel/tests/process_elf_isolation_fixture.o

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
		$(if $(filter 1,$(COMPDB_CAPTURE)),-MJ $@.json,) \
		-MMD \
		-MP \
		-MF $(@:.o=.d) \
		-MT $@ \
		-c $< \
		-o $@

$(KERNEL_OBJ_DIR)/c/kernel/runtime/memory.o: CFLAGS += \
	-fno-builtin-memcpy \
	-fno-builtin-memset

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

$(PROCESS_SYSCALL_ABI_FIXTURE_OBJ): $(SYSCALL_ABI_ELF)

$(PROCESS_SYSCALL_POINTER_FIXTURE_OBJ): $(SYSCALL_POINTER_ELF)

$(PROCESS_FD_SYSCALLS_FIXTURE_OBJ): $(FD_SYSCALLS_ELF)

$(PROCESS_SCHEDULER_CONTEXT_FIXTURE_OBJ): $(SCHEDULER_CONTEXT_ELF)

$(PROCESS_EXEC_FIXTURE_OBJ): $(EXEC_CALLER_ELF) $(EXEC_TARGET_ELF)

$(PROCESS_WAITPID_FIXTURE_OBJ): \
	$(WAITPID_PARENT_ELF) \
	$(WAITPID_CHILD_ELF)

$(PROCESS_FORK_FIXTURE_OBJ): $(FORK_ELF)

$(PROCESS_ELF_PROTECTION_FIXTURE_OBJ): \
	$(ELF_SELF_MODIFY_ELF)

$(PROCESS_ELF_ISOLATION_FIXTURE_OBJ): \
	$(ELF_ISOLATION_ATTACKER_ELF) \
	$(ELF_ISOLATION_VICTIM_ELF)
endif

$(KERNEL_ELF): $(KERNEL_OBJS) $(LINKER_SCRIPT)
	$(LD_LLD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

-include $(KERNEL_DEPS)

# Limine protocol
.PHONY: limine-protocol

limine-protocol: $(LIMINE_HEADER)

$(LIMINE_HEADER): $(LIMINE_PROTOCOL_FETCH_SCRIPT)
	./$(LIMINE_PROTOCOL_FETCH_SCRIPT)

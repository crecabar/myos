# -----------------------------------------------------------------------------
# Toolchain
# -----------------------------------------------------------------------------

TARGET := x86_64-unknown-none-elf
MYOS_INCLUDE_DIR := include

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

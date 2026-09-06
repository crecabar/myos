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

.PHONY: check-toolchain clean

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

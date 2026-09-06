# MyOS

MyOS is a small educational Unix-like operating system project for x86-64.

The project exists to learn how an operating system is built from the ground up, from boot and kernel entry to memory management, processes, syscalls, filesystems, libc, and eventually a small Unix-style userland.

The goal is not to compete with Linux, BSD, or modern production operating systems.

> I do not want to build the next Linux. I want to understand how one is built.

## Current target

MyOS currently targets:

```text
Architecture: x86-64
Firmware:     UEFI
Bootloader:   Limine
Kernel:       C
Assembler:    only where necessary
Executable:   ELF64
Compiler:     LLVM/Clang 21
Linker:       LLD 21
Emulator:     QEMU
Debugger:     GDB
Build system: GNU Make
CPU count:    1
Kernel model: small monolithic kernel
```

Development currently takes place on an ARM64 macOS host, so the project is cross-compiled from the beginning.

## Project philosophy

MyOS deliberately prioritizes:

- learning;
- explicitness;
- simplicity;
- understanding the hardware;
- small and readable data structures;
- reproducible debugging;
- clear invariants;
- incremental development;
- minimal abstraction until abstraction becomes necessary.

The project should advance in small, verifiable steps.

Simple implementations are preferred before sophisticated ones.

Examples:

- a bump allocator before a complex allocator;
- round-robin scheduling before advanced priorities;
- a simple filesystem before ext2;
- a small custom shell before Bash;
- one CPU before SMP;
- minimal drivers before broad hardware support.

## Current status

The project is currently in:

```text
Milestone 0 — Toolchain and reproducible environment
```

The current objective is to establish a reproducible development workflow capable of:

```text
source
  |
  v
x86-64 freestanding object files
  |
  v
kernel ELF
  |
  v
bootable image
  |
  v
QEMU
  |
  v
GDB
```

No kernel functionality is implemented yet.

## Toolchain

The local development environment can be verified with:

```bash
make check-toolchain
```

Detailed toolchain notes are available in:

```text
docs/toolchain.md
```

## Near-term milestones

The first two milestones are intentionally small.

### Milestone 0

Prepare:

- compiler;
- linker;
- ELF inspection tools;
- Makefile;
- linker script;
- Limine/UEFI boot integration;
- bootable image generation;
- QEMU execution;
- GDB remote debugging.

### Milestone 1

Reach the first owned C code running as a kernel and produce an observable result:

```text
Hello from kernel
```

Nothing beyond that belongs in the first kernel milestone.

## License

MyOS is free software licensed under the GNU General Public License, version 2 only.

See [`LICENSE`](LICENSE) for the complete license text.

SPDX identifier:

```text
GPL-2.0-only
```

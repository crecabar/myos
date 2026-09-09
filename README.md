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

The kernel is built as a freestanding x86-64 target and does not depend on the development host architecture.

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

- a simple physical frame allocator before a general heap allocator;
- round-robin scheduling before advanced priorities;
- a simple filesystem before ext2;
- a small custom shell before Bash;
- one CPU before SMP;
- minimal drivers before broad hardware support.

## Current status

MyOS has moved beyond initial bootstrapping and is currently building its memory-management foundation.

The current boot sequence is able to initialize the boot environment, physical memory manager, framebuffer console, architecture support, and runtime diagnostics:

```text
MyOS 0.1

[boot] Environment initialized
[memory] physical memory initialized
[display] Console initialized
[arch] x86-64 initialized
[kernel] Initialization complete
```

Implemented foundations currently include:

- UEFI boot through Limine;
- higher-half ELF64 kernel entry;
- serial diagnostics;
- framebuffer console output;
- centralized diagnostics and formatting;
- kernel panic and halt support;
- x86-64 IDT initialization;
- exception diagnostics for divide errors and page faults;
- CR3 and page-table introspection;
- 4 KiB, 2 MiB, and 1 GiB page translation inspection;
- boot memory-map normalization into MyOS-owned structures;
- higher-half direct-map abstraction;
- physical-frame bitmap management;
- physical frame allocation and release;
- MyOS-owned page-table allocation;
- page-table entry construction;
- page-address-space creation;
- virtual-to-physical page mapping through a reusable paging API.

MyOS currently allocates only regions marked usable by the bootloader. Regions marked `bootloader-reclaimable` remain reserved until the kernel can prove that no required Limine-owned structures are still referenced.

The active address space is still the one prepared by Limine. MyOS can already construct complete independent x86-64 page-table hierarchies outside the active CR3 and map 4 KiB virtual pages into them, but it does not yet switch CR3 to a MyOS-owned address space.

## Development milestones

### Milestone 0 — Toolchain and reproducible environment ✅

Completed:

- compiler and linker setup;
- ELF inspection tools;
- GNU Make build system;
- linker script;
- Limine/UEFI boot integration;
- bootable image generation;
- QEMU execution;
- GDB remote debugging.

### Milestone 1 — First owned kernel code ✅

Completed:

- enter freestanding C kernel code;
- establish early serial diagnostics;
- produce observable kernel output;
- halt safely under kernel control.

The original milestone target was deliberately small:

```text
Hello from kernel
```

The kernel has since progressed well beyond this point.

### Milestone 2 — Architecture and diagnostics foundation ✅

Completed:

- framebuffer-backed console;
- diagnostics fan-out to serial and framebuffer sinks;
- reusable formatting layer;
- architecture initialization boundary;
- 256-entry x86-64 IDT;
- normalized exception frames;
- divide-error and page-fault diagnostics;
- kernel panic path;
- paging introspection and virtual-address translation diagnostics.

### Milestone 3 — Physical and virtual memory management 🚧

In progress.

Completed so far:

- normalize Limine memory-map regions into MyOS-owned types;
- isolate direct-map translation from bootloader-specific APIs;
- identify and count usable physical frames;
- create a self-reserving physical-frame bitmap;
- allocate and release 4 KiB physical frames;
- allocate zeroed page-table frames;
- construct x86-64 table and page entries;
- create independent address spaces with their own PML4 root;
- construct complete PML4 → PDPT → PD → PT → page hierarchies;
- map a 4 KiB virtual page to an arbitrary physical frame with `paging_map_page()`.

Next work in this milestone includes:

- unmapping pages;
- explicit page-permission handling and validation;
- lifecycle management for page-table structures;
- activating a MyOS-owned address space through CR3;
- reclaiming bootloader-reclaimable memory when it is safe to do so.

## Toolchain

The local development environment can be verified with:

```bash
make check-toolchain
```

Detailed toolchain notes are available in:

```text
docs/toolchain.md
```

## Longer-term direction

Once the memory-management foundation is stable, the project will continue toward:

```text
kernel-owned virtual memory
        ↓
process address spaces
        ↓
scheduler and context switching
        ↓
userspace transition
        ↓
system calls
        ↓
filesystem and libc
        ↓
small Unix-style userland
```

Each stage will continue to be developed in small, observable, and independently verifiable steps.

## License

MyOS is free software licensed under the GNU General Public License, version 2 only.

See [`LICENSE`](LICENSE) for the complete license text.

SPDX identifier:

```text
GPL-2.0-only
```

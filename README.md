# MyOS

MyOS is a small educational Unix-like operating system project for x86-64.

The project exists to learn how an operating system is built from the ground up, from boot and kernel entry to memory management, privilege separation, processes, syscalls, filesystems, libc, and eventually a small Unix-style userland.

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
Executable:   ELF64 kernel; userspace ELF loading is planned
Compiler:     LLVM/Clang 21
Linker:       LLD 21
Emulator:     QEMU
Debugger:     GDB
Build system: GNU Make
CPU count:    1
Kernel model: small monolithic kernel
```

The kernel is built as a freestanding `x86_64-unknown-none-elf` target and does not depend on the development host architecture.

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

The project advances in small, observable, independently verifiable steps. Simple implementations are preferred before sophisticated ones.

Examples:

- a bitmap physical-frame allocator before a general heap allocator;
- fixed-quantum round-robin scheduling before advanced priorities;
- one CPU before SMP;
- embedded user programs before an ELF loader;
- a small filesystem before a general-purpose disk filesystem;
- a small custom shell before a full Unix shell;
- minimal hardware support before broad device compatibility.

## Current status

MyOS has moved beyond bootstrapping and basic memory management. The current kernel can enter ring 3, execute multiple user processes in independent address spaces, receive syscalls through `int 0x80`, and preempt user processes using a PIT-driven timer routed through the IOAPIC/LAPIC path.

A current boot reaches a sequence conceptually like:

```text
MyOS 0.1

[boot] Environment initialized
[memory] ...
[paging] MyOS address space active
[display] Console initialized
[arch] x86-64 initialized
[scheduler] Initialized
[kernel] Initialization complete
[clock] ... UTC
[process] User memory copy test passed
[kernel] Starting scheduler
[scheduler] Running PID 1
...
[scheduler] Running PID 2
...
```

The userspace programs used today are still small machine-code payloads embedded in the kernel. They exist to validate privilege transitions, address-space switching, syscalls, timer preemption, and process context restoration before the first ELF loader is introduced.

### Implemented foundations

#### Boot, diagnostics, and display

- UEFI boot through Limine;
- higher-half ELF64 kernel entry;
- normalized boot information copied into MyOS-owned structures;
- serial diagnostics;
- 32-bit framebuffer console;
- centralized diagnostics and formatting;
- RTC/CMOS time reading;
- kernel panic and halt support;
- optional runtime diagnostics.

#### Physical and virtual memory

- higher-half direct-map abstraction;
- physical-frame bitmap allocator;
- allocator self-reservation;
- physical frame allocation and release;
- MyOS-owned page-table allocation;
- 4 KiB page mapping and unmapping;
- reclaim of empty intermediate PT/PD/PDPT tables;
- virtual-to-physical translation for 4 KiB, 2 MiB, and 1 GiB mappings;
- explicit writable/user/execute permissions;
- NX capability detection and `EFER.NXE` enablement;
- activation of a MyOS-owned PML4 through CR3;
- per-process PML4 roots;
- private lower-half process mappings with shared higher-half kernel mappings;
- user code pages mapped read-only/executable;
- user stacks mapped read/write/non-executable;
- unmapped stack guard page;
- process-memory read/write helpers through physical translation.

MyOS still retains the boot-time higher-half paging branches inherited through Limine. Fully rebuilding those mappings under exclusive kernel ownership and reclaiming bootloader-reclaimable memory remain part of the memory-management work.

#### x86-64 execution and interrupts

- kernel-owned GDT;
- ring-0 and ring-3 code/data descriptors;
- kernel-owned TSS;
- 256-entry IDT;
- normalized interrupt frames shared between assembly and C;
- divide-error and page-fault handling;
- `iretq` transition from CPL0 to CPL3;
- legacy PIC disabling;
- Local APIC initialization;
- IOAPIC initialization and routing;
- PIT timer at 100 Hz;
- spurious-interrupt handling;
- timer-driven entry into the scheduler.

The interrupt topology is intentionally QEMU/Q35-specific for now. ACPI/MADT discovery is future portability work before broad real-hardware support.

#### Processes, syscalls, and scheduling

- process descriptors with PID, execution context, memory, layout, and state;
- independent process address spaces and CR3 switching;
- initial user code and stack layout;
- ring-3 execution of embedded user programs;
- syscall entry through `int 0x80`;
- current syscall ABI using `RAX` for the syscall number and registers for arguments;
- `DEBUG_PUTC`, `WRITE`, `EXIT`, and `YIELD` syscalls;
- kernel-side copying from user memory for `WRITE`;
- cooperative `yield()`;
- fixed-quantum preemptive round-robin scheduling;
- timer preemption of CPL3 execution;
- save/restore of general-purpose register state plus RIP/RSP/RFLAGS;
- process termination on explicit exit;
- user page-fault termination path;
- switching to another process by rewriting the active interrupt frame and returning through `iretq`.

The current scheduler uses a small fixed process table and does not yet implement blocked/sleeping states, dynamic process ownership, resource reaping, or kernel threads.

## Current hardening pass

The mechanisms for paging, ring 3, syscalls, and preemptive scheduling are now functionally demonstrated. Before adding an ELF loader or exposing more general memory-management syscalls, MyOS is intentionally pausing feature expansion to turn several currently implicit invariants into enforced boundaries.

The immediate hardening work is:

- define and enforce canonical user virtual-address bounds for all `process_memory_*` operations;
- prevent process operations from modifying or reclaiming higher-half kernel-shared paging branches;
- reject huge-page entries in walkers that specifically expect a lower-level 4 KiB page table;
- expand CPU exception coverage so faults such as `#UD`, `#GP`, `#SS`, and related exceptions from CPL3 terminate only the offending process;
- give `#DF` a dedicated IST stack;
- establish process lifecycle/reaping so terminated processes release pages, page tables, and scheduler slots;
- add a minimal kernel heap so process objects no longer depend on static or `kernel_main()`-lifetime storage;
- add clipping to framebuffer primitives before introducing the boot-mascot image blitter;
- explicitly prohibit kernel/userspace FP/SIMD for now, or later add per-context FPU/SSE state management before allowing it;
- expand runtime/QEMU tests for isolation, process lifecycle, and context switching.

Until this pass is complete, current CPL3 programs should be treated as trusted internal test payloads rather than a hardened hostile-userspace boundary.

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

Completed for the original scope:

- framebuffer-backed console;
- diagnostics fan-out to serial and framebuffer sinks;
- reusable formatting layer;
- architecture initialization boundary;
- x86-64 IDT infrastructure;
- normalized exception frames;
- kernel panic path;
- paging introspection and runtime diagnostics.

### Milestone 3 — Kernel-owned virtual memory 🚧 Nearly complete

Implemented:

- physical-frame allocation/release;
- page-table construction;
- map/unmap;
- page-table reclamation;
- NX and page permissions;
- MyOS-owned active PML4/CR3;
- process address spaces with shared higher-half kernel mappings;
- process code/stack mappings.

Remaining work includes:

- user-address/ownership hardening;
- huge-page safety in 4 KiB walkers;
- fully kernel-owned higher-half mappings;
- safe reclaim of bootloader-reclaimable memory.

### Milestone 4 — Interrupts, timer, and kernel heap 🚧 Partially functional

Implemented:

- GDT/TSS;
- IDT infrastructure;
- PIC disable;
- LAPIC/IOAPIC path;
- PIT timer at 100 Hz;
- hardware interrupts enabled;
- RTC support.

Remaining work includes:

- broad CPU exception coverage;
- dedicated IST handling for critical exceptions;
- a minimal kernel heap;
- eventual ACPI/MADT-based interrupt topology discovery.

### Milestone 5 — Scheduler 🚧 Functionally demonstrated

Implemented:

- multiple user processes;
- resumable CPU contexts;
- cooperative yield;
- preemptive fixed-quantum round-robin scheduling;
- CR3 switching;
- timer-driven preemption;
- process exit/termination states.

Remaining work includes:

- resource reaping and slot reuse;
- blocked/sleeping states;
- interruptible idle behavior;
- later separation of process and thread execution contexts when required.

### Milestone 6 — Ring 3 and syscalls 🚧 Functionally demonstrated

Implemented:

- CPL3 entry through `iretq`;
- ring-3 code and stack mappings;
- `int 0x80` syscall entry;
- initial syscall ABI;
- `DEBUG_PUTC`, `WRITE`, `EXIT`, and `YIELD`;
- return/resume through interrupt frames.

Remaining work is primarily hardening:

- complete exception containment for hostile user code;
- stronger user-pointer/address validation;
- continued ABI discipline as more syscalls are added.

### Milestone 7 — Processes and ELF loading 🚧 Foundations only

Already available:

- process descriptors;
- process address spaces;
- user layout and stacks;
- process scheduling and termination mechanisms.

Next major feature work, after the hardening pass:

- ELF64 validation and loading;
- mapping ELF segments with derived permissions;
- initial `argc`/`argv`/`envp` stack construction;
- dynamic process creation/lifecycle;
- eventual `execve`, `wait`, and a simple first `fork` implementation.

### Milestones 8–14 — Road to `startx` / `xclock`

The long-term critical path is tracked in [Roadmap: MyOS → startx → xclock](https://github.com/crecabar/myos/issues/13):

```text
M8   VFS, initramfs, file descriptors
 ↓
M9   minimal Unix userland and shell
 ↓
M10  POSIX primitives required by X11
 ↓
M11  userspace framebuffer and input devices
 ↓
M12  libc and cross-porting platform
 ↓
M13  minimal native X11 server
 ↓
M14  startx-ready system
 ↓
xclock
```

The goal is deliberately not to implement every Unix or POSIX feature in advance. Compatibility work will be driven by concrete dependencies required by real programs.

## Near-term development order

The intended sequence from the current state is:

```text
Hardening Pass 1
  ├─ user-address and page-table ownership enforcement
  ├─ exception containment + #DF IST
  ├─ process lifecycle/reaping
  ├─ kernel heap
  └─ regression tests
          ↓
ELF64 loader
          ↓
/init
          ↓
VFS + file descriptors
          ↓
minimal Unix userland
          ↓
POSIX/IPC surface required by X11
          ↓
userspace display/input
          ↓
X11 server
          ↓
startx + xclock
```

## Toolchain

The local development environment can be verified with:

```bash
make check-toolchain
```

Build the kernel with:

```bash
make
```

Build the bootable ISO with:

```bash
make iso
```

Run MyOS in QEMU with:

```bash
make run
```

Detailed toolchain notes are available in:

```text
docs/toolchain.md
```

## Hardware scope

Development currently targets QEMU Q35 with one x86-64 CPU. The scheduler, physical allocator, interrupt infrastructure, and other shared kernel state currently rely on this single-core assumption.

Running on real x86-64 hardware remains an explicit project goal, but broad hardware support is not part of the immediate critical path. Before that stage, MyOS will need at least ACPI/MADT-based interrupt discovery and additional hardware-specific validation.

## North Star

The immediate engineering goal is not "build Linux" or "implement all of POSIX". The long-term demonstration target is intentionally concrete:

```text
MyOS $ startx
```

followed by a working X11 session capable of launching:

```text
xclock
```

Getting there requires progressively turning today's small kernel mechanisms into a coherent Unix-like system while keeping each intermediate step understandable and testable.

## License

MyOS is free software licensed under the GNU General Public License, version 2 only.

See [`LICENSE`](LICENSE) for the complete license text.

SPDX identifier:

```text
GPL-2.0-only
```

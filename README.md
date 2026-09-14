# MyOS

MyOS is a small educational Unix-like operating system project for x86-64.

The project exists to learn how an operating system is built from the ground up, from boot and kernel entry to memory management, privilege separation, processes, syscalls, filesystems, libc, and eventually a small Unix-style userland.

The goal is not to compete with Linux, BSD, or modern production operating systems.

> I do not want to build the next Linux. I want to understand how one is built.

## Current target

MyOS currently targets:

```text
Architecture:       x86-64
Firmware:           UEFI
Bootloader:         Limine
Kernel:             C
Assembler:          only where necessary
Executable:         ELF64 kernel; userspace ELF loading is planned
Compiler:           LLVM/Clang 21
Linker:             LLD 21
Emulator:           QEMU
Validated hardware: Dell Xeon workstation via UEFI USB boot
Debugger:           GDB
Build system:       GNU Make
CPU count:          1
Kernel model:       small monolithic kernel
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

MyOS has moved beyond bootstrapping and basic memory management. The current kernel can enter ring 3, execute multiple user processes in independent address spaces, receive syscalls through `int 0x80`, preempt user processes using a PIT-driven timer routed through the IOAPIC/LAPIC path, and contain a broad set of user-originated CPU exceptions without bringing down the kernel.

On 2026-09-13, MyOS also completed its first successful boot on physical x86-64 hardware: a Dell Xeon workstation booting the kernel from a UEFI USB image. The full runtime diagnostics and kernel test suite completed successfully, and the scheduler reached the expected idle state after all test processes terminated.

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
[kernel] Starting scheduler
[scheduler] Running PID ...
...
[process] PID ... terminated: ...
[scheduler] Running PID ...
...
```

The userspace programs used today are still small x86-64 machine-code payloads, but they now live as architecture-specific kernel test fixtures rather than normal process code. They validate privilege transitions, address-space switching, syscalls, timer preemption, process context restoration, and exception containment before the first ELF loader is introduced.

### 2026-09-13 — First successful boot on physical hardware ✅

MyOS successfully booted on a Dell Xeon workstation from a GPT-partitioned UEFI USB image for the first time.

The physical-hardware run completed the same runtime diagnostics and hostile userspace regression tests used under QEMU, including:

- physical-memory allocator and paging diagnostics;
- page-table ownership and userspace-boundary hardening tests;
- process-memory range, stack, and layout lifecycle tests;
- GDT/TSS validation;
- independent process address spaces and CR3 switching;
- ring-3 execution and syscall handling;
- preemptive round-robin scheduling;
- user-originated `#PF`, `#UD`, `#GP`, and `#NM` exception containment;
- termination of offending CPL3 processes without bringing down the kernel;
- survivor-process execution after hostile-process faults.

After the test processes completed or were intentionally terminated, the scheduler reached:

```text
[scheduler] No runnable processes
[scheduler] System idle
```

This milestone confirms that the current kernel mechanisms are not limited to the QEMU execution environment. It does not imply broad hardware portability yet: interrupt discovery and other platform details still need to move toward ACPI/MADT-driven configuration before MyOS can claim general x86-64 hardware support.

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
- optional runtime diagnostics;
- bootable ISO generation;
- bootable GPT/EFI USB image generation for QEMU and physical hardware.

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
- process-memory read/write helpers through physical translation;
- validated user-memory copy-in for buffered syscalls, including overflow protection and 4 KiB USER-page checks.

MyOS still retains the boot-time higher-half paging branches inherited through Limine. Fully rebuilding those mappings under exclusive kernel ownership and reclaiming bootloader-reclaimable memory remain part of the memory-management work.

#### x86-64 execution and interrupts

- kernel-owned GDT;
- ring-0 and ring-3 code/data descriptors;
- kernel-owned TSS;
- 256-entry IDT;
- normalized interrupt frames shared between assembly and C;
- broad architectural exception coverage including `#DE`, `#DB`, NMI, `#BP`, `#OF`, `#BR`, `#UD`, `#NM`, `#DF`, `#TS`, `#NP`, `#SS`, `#GP`, `#PF`, `#MF`, `#AC`, `#MC`, `#XM`, `#VE`, and `#CP`;
- CPL3 exception containment that terminates the offending process where appropriate while keeping kernel-origin faults fatal;
- dedicated TSS IST stack for `#DF`;
- explicit FP/SIMD trap-on-use policy through `CR0.TS` and `#NM` until per-context FP/SIMD state exists;
- `iretq` transition from CPL0 to CPL3;
- legacy PIC disabling;
- Local APIC initialization;
- IOAPIC initialization and routing;
- PIT timer at 100 Hz;
- spurious-interrupt handling;
- timer-driven entry into the scheduler.

The current interrupt topology is still intentionally simple and not generally discoverable at runtime. The successful Dell workstation boot validates the present path on that machine, but ACPI/MADT discovery remains future portability work before broad real-hardware support.

#### Processes, syscalls, and scheduling

- process descriptors with PID, execution context, memory, layout, and state;
- independent process address spaces and CR3 switching;
- initial user code and stack layout;
- ring-3 execution of embedded user programs;
- syscall entry through `int 0x80`;
- explicit six-register x86-64 syscall ABI: `RAX` for the syscall number/result and `RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9` for arguments 0–5;
- `DEBUG_PUTC`, `WRITE`, `EXIT`, and `YIELD` syscalls;
- kernel-side copying from user memory for `WRITE`;
- cooperative `yield()`;
- fixed-quantum preemptive round-robin scheduling;
- timer preemption of CPL3 execution;
- save/restore of general-purpose register state plus RIP/RSP/RFLAGS;
- process termination on explicit exit;
- explicit process termination reasons;
- exception-driven termination of hostile CPL3 processes, including invalid memory access, `ud2`, privileged `hlt`, and x87 use under the current `#NM` policy;
- switching to another process by rewriting the active interrupt frame and returning through `iretq`;
- compile-time kernel test gating through `MYOS_KERNEL_TESTS`, separate from runtime diagnostics.

The current scheduler uses a small fixed process table and does not yet implement blocked/sleeping states, dynamic process ownership, resource reaping, or kernel threads.

## Current hardening pass

The mechanisms for paging, ring 3, syscalls, and preemptive scheduling are now functionally demonstrated. Before adding an ELF loader or exposing more general memory-management syscalls, MyOS is intentionally pausing feature expansion to turn several currently implicit invariants into enforced boundaries.

The immediate hardening work is now concentrated on:

- define and enforce canonical user virtual-address bounds for all `process_memory_*` operations;
- prevent process operations from modifying or reclaiming higher-half kernel-shared paging branches;
- reject huge-page entries in walkers that specifically expect a lower-level 4 KiB page table;
- establish process lifecycle/reaping so terminated processes release pages, page tables, and scheduler slots;
- add a minimal kernel heap so process objects no longer depend on static or `kernel_main()`-lifetime storage;
- add clipping to framebuffer primitives before introducing the boot-mascot image blitter;
- expand paging-isolation, lifecycle, and automated QEMU regression tests.

The exception-containment side of this pass is now substantially implemented: broad x86-64 exception coverage is installed, appropriate CPL3 faults terminate only the offending process, `#DF` uses a dedicated IST stack, initial user RFLAGS are explicit, and FP/SIMD is deliberately trapped through `#NM` until per-context state management exists.

The kernel now deliberately runs hostile CPL3 regression fixtures, but the userspace boundary should still be considered incomplete until canonical address limits, shared page-table ownership, and process resource reclamation are enforced.

## Development milestones

### Milestone 0 — Toolchain and reproducible environment ✅

Completed:

- compiler and linker setup;
- ELF inspection tools;
- GNU Make build system;
- linker script;
- Limine/UEFI boot integration;
- bootable ISO and UEFI USB image generation;
- QEMU execution;
- GDB remote debugging;
- first successful UEFI USB boot on physical x86-64 hardware.

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

### Milestone 4 — Interrupts, timer, and kernel heap 🚧 Interrupt/exception foundation substantially complete

Implemented:

- GDT/TSS;
- IDT infrastructure;
- broad x86-64 architectural exception coverage;
- CPL3 exception containment with kernel-fatal CPL0 faults;
- dedicated `#DF` IST stack;
- FP/SIMD trap-on-use policy (`CR0.TS` → `#NM`);
- PIC disable;
- LAPIC/IOAPIC path;
- PIT timer at 100 Hz;
- hardware interrupts enabled;
- RTC support.

Remaining work includes:

- sleep/delay primitives driven by timer ticks;
- a minimal kernel heap;
- heap lifetime/integrity diagnostics;
- eventual ACPI/MADT-based interrupt topology discovery.

### Milestone 5 — Scheduler 🚧 Functionally demonstrated

Implemented:

- multiple user processes;
- resumable CPU contexts;
- cooperative yield;
- preemptive fixed-quantum round-robin scheduling;
- CR3 switching;
- timer-driven preemption;
- process exit/termination states and explicit termination reasons;
- exception-driven process termination from the active interrupt frame;
- hostile-process/survivor regression fixtures proving scheduling continues after user faults.

Remaining work includes:

- resource reaping and slot reuse;
- blocked/sleeping states;
- interruptible idle behavior;
- later separation of process and thread execution contexts when required.

### Milestone 6 — Ring 3 and syscalls 🚧 Functionally demonstrated and materially hardened

Implemented:

- CPL3 entry through `iretq`;
- ring-3 code and stack mappings;
- `int 0x80` syscall entry;
- six-register x86-64 syscall argument convention (`RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9`) with syscall number/result in `RAX`;
- `DEBUG_PUTC`, `WRITE`, `EXIT`, and `YIELD`;
- validated buffered `WRITE` copy-in from the calling process;
- explicit initial user RFLAGS (`0x202`);
- broad CPL3 CPU-exception containment;
- return/resume through rewritten interrupt frames.

Remaining work is primarily hardening:

- enforce canonical user virtual-address bounds across all process-memory APIs;
- enforce page-table ownership so process operations cannot affect shared higher-half mappings;
- complete syscall preservation/clobber and stable error-return conventions;
- add active invalid-pointer/range regressions.

### Milestone 7 — Processes and ELF loading 🚧 Foundations only

Already available:

- process descriptors with termination reasons;
- process address spaces;
- user layout and stacks;
- process scheduling and exception-contained termination mechanisms;
- documented kernel-side syscall register ABI and buffered user-memory `WRITE`;
- architecture-specific embedded user-program test fixtures.

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
  ├─ canonical user-address bounds + page-table ownership enforcement
  ├─ huge-page guards in 4 KiB-only walkers
  ├─ process lifecycle/reaping + scheduler slot reuse
  ├─ kernel heap
  ├─ framebuffer clipping / bounds safety
  └─ stronger isolation/lifecycle regressions
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

Build the bootable UEFI USB image with:

```bash
make usb-image
```

Build a UEFI USB image with runtime diagnostics and kernel tests enabled with:

```bash
make usb-image-diagnostics
```

Run MyOS in QEMU from the ISO with:

```bash
make run
```

Run the USB image in QEMU as USB mass storage with:

```bash
make run-usb
```

Run the diagnostic/test USB image in QEMU with:

```bash
make run-usb-diagnostics
```

### Writing the USB image to physical media

On macOS, first identify the target USB device:

```bash
diskutil list
```

**Warning:** writing the image directly to a device destroys the existing partition table and data on that device. Device identifiers are not stable across reconnects, so verify the target every time before running `dd`.

For the first physical-hardware boot, the USB stick appeared as `/dev/disk6`. After building the desired image, unmount the whole device without ejecting it:

```bash
diskutil unmountDisk /dev/disk6
```

Then write the image through the corresponding raw device:

```bash
sudo dd \
    if=build/myos-usb.img \
    of=/dev/rdisk6 \
    bs=1048576
```

Flush outstanding writes and eject the device cleanly:

```bash
sync
diskutil eject /dev/disk6
```

The `/dev/disk6` / `/dev/rdisk6` identifiers above are examples from the first successful Dell workstation boot. Replace the disk number with the device reported by `diskutil list` on the current machine.

Detailed toolchain notes are available in:

```text
docs/toolchain.md
```

## Hardware scope

Development primarily targets QEMU Q35 with one x86-64 CPU. The scheduler, physical allocator, interrupt infrastructure, and other shared kernel state currently rely on this single-core assumption.

As of 2026-09-13, MyOS has also booted successfully on a physical Dell Xeon workstation from a UEFI USB image. The full runtime diagnostics and kernel test suite passed on that machine, including userspace execution, timer-driven scheduling, process isolation, paging hardening checks, and exception containment.

This is a validated physical-hardware milestone, not a claim of broad hardware compatibility. MyOS still needs ACPI/MADT-based interrupt discovery and additional platform validation before general x86-64 hardware support is a realistic goal.

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

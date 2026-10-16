# MyOS

MyOS is a small educational Unix-like operating system project for x86-64.

The project exists to learn how an operating system is built from the ground up, from boot and kernel entry to memory management, privilege separation, processes, syscalls, filesystems, an initial C runtime, and a small Unix-style userland.

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
Executable:         ELF64 kernel and statically linked ELF64 userspace programs
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

## Current status — MyOS 0.3.0

MyOS now boots into an **interactive userspace shell** (`myos $`). This is a working path through the kernel, process scheduler, VFS, file descriptors, the native syscall ABI, a minimal C runtime, and independently built ELF64 programs—not a shell embedded in the kernel.

On a normal boot, the kernel mounts the read-only initramfs as the system root, mounts devfs at `/dev`, and starts `/init` in ring 3. The userspace init process configures stdin from `/dev/console`, launches `/bin/sh`, waits for it, and starts another shell when it exits normally.

For example:

```text
MyOS 0.3.0
...
init: starting shell
MyOS shell bootstrap
myos $ uname -r
0.3.0
myos $ pwd
/
myos $ cd /dev
myos $ pwd
/dev
myos $ cd /
myos $ echo Hello from MyOS
Hello from MyOS
myos $
```

The shell supports a deliberately small whitespace-separated command/argument syntax, `cd` and `exit` builtins, foreground external programs from `/bin`, and child exit-status tracking. Canonical console input supports echo, bounded Backspace editing, blocking reads, and Ctrl+D. In particular, Ctrl+D flushes pending text without appending a newline; pressing it on an empty input segment delivers EOF.

**2026-10-15 — First interactive userspace shell:** [PR #332](https://github.com/crecabar/myos/pull/332), closing [issue #60](https://github.com/crecabar/myos/issues/60), includes a recorded QEMU demonstration. This completes the *initial shell* milestone; the broader [M9 epic](https://github.com/crecabar/myos/issues/7) remains open for [#61](https://github.com/crecabar/myos/issues/61) (pipes, redirection, background jobs, and environment propagation).

MyOS is not Linux ABI compatible, not a complete POSIX implementation, and not yet a general-purpose desktop operating system. Its longer-term demonstration target is a native `startx` session and `xclock`, followed eventually by self-hosted development.

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

This milestone confirms that early kernel mechanisms were not limited to QEMU. Since this first hardware boot, MyOS has added ACPI RSDP/XSDT/MADT discovery and interrupt-topology handling; broader UEFI hardware compatibility still needs independent validation. The 0.3.0 interactive shell demonstration was performed under QEMU and should not be assumed to be validated on every physical PC.

### Implemented foundations

#### Boot, hardware, and console

- x86-64 UEFI boot through Limine, with bootloader-supplied kernel command-line settings;
- ELF64 higher-half kernel, serial diagnostics, kernel panic handling, and QEMU/UEFI ISO and USB images;
- normalized boot information, physical-memory reclamation, and ACPI RSDP/XSDT/MADT discovery;
- GDT/TSS, IDT, exception containment, LAPIC/IOAPIC routing, and a PIT-driven 100 Hz timer;
- PS/2 keyboard and mouse input, normalized keyboard state, and Ctrl+Alt+Delete reset;
- 32-bit framebuffer text console, configurable integer font scale, canonical input with echo/editing, Ctrl+D EOF, and separate kernel diagnostics.

#### Memory and execution

- physical-frame allocator, kernel heap, kernel-managed paging and higher-half direct map;
- per-process PML4 roots and private userspace memory, with shared kernel mappings, NX permissions, guard pages, and validated user-memory copying;
- ELF64 validation/loading, initial userspace stacks, and program execution from VFS;
- ring-3 transitions, an independent MyOS syscall ABI through `int 0x80`, and userspace memory/descriptor/process services;
- timer-preemptive single-CPU scheduling, blocked-process wakeups, timer sleeps, process lifecycle/reaping, and copy-based `fork`, `execve`, and `waitpid`.

#### Filesystems, devices, and userspace

- VFS nodes, mount topology, path resolution, open-file objects, file-descriptor tables, and per-process root/current working directory;
- a read-only CPIO initramfs mounted at `/` and devfs mounted at `/dev` with `/dev/null`, `/dev/zero`, and `/dev/console`;
- character-device registry and bus/device-driver discovery foundations;
- a small freestanding userspace C startup/runtime, string/memory helpers, allocation, formatting, and syscall wrappers;
- userspace `/init` supervising `/bin/sh`, which runs builtins and foreground ELF programs;
- initial utilities: `true`, `false`, `echo`, `pwd`, `cat`, and `uname`.

Tests exercise memory and paging boundaries, process isolation, ELF loading, schedulers and syscalls, device/VFS contracts, userspace runtime and commands, input, and EOF/backpressure behavior. Deliberately hostile ring-3 fixtures can trigger CPU faults as *expected regression results*; an exception log by itself is not evidence of a failing run.

#### Current limitations

MyOS remains educational and intentionally constrained: one CPU, static userspace ELF binaries, a small bootstrap libc/runtime, and a framebuffer text console. The initramfs is read-only and there is no mature persistent native filesystem or graphical desktop in this release. A complete terminal/TTY subsystem, POSIX compatibility, pipes, redirection, job control, environment propagation, broader device support, and SMP remain future work. See the issues and roadmap for the intended dependency order.

## Development milestones

Development has progressed from the earliest boot and architecture experiments to a real userland:

| Milestone | Status in 0.3.0 |
| --- | --- |
| M0–M2 — Toolchain, kernel entry, diagnostics | Initial scope complete |
| M3–M4 — Memory, interrupts, kernel heap | Functional foundations implemented; further hardening continues |
| M5–M6 — Scheduler, ring 3, syscalls | Functional with blocking, preemption, and regression coverage |
| M7 — ELF and process execution | Initial ELF64 load, fork, exec, wait, and lifecycle available |
| M8 — VFS, initramfs, file descriptors | First usable namespace and descriptor layer available |
| M9 — Minimal userland and shell | `/init`, `myos $`, and external commands demonstrated; #61 remains |
| M10–M14 — Toward graphical userland | Planned in the roadmap |

The original first-kernel demonstration was simply:

```text
Hello from kernel
```

The current release reaches a process-managed userspace shell. Milestone labels describe achieved engineering capability, not a claim that every possible API within that area is complete.

### Road to `startx` and `xclock`

The long-term critical path is tracked in [MyOS → startx → xclock](https://github.com/crecabar/myos/issues/13). The next shell extension is [#61](https://github.com/crecabar/myos/issues/61); beyond it, MyOS needs additional IPC/POSIX facilities, a more mature libc/porting environment, userspace display/input interfaces, and an X11-compatible session path.

```text
MyOS kernel → /init → myos $                  (achieved)
                         ↓
              pipes, redirection, jobs        (#61)
                         ↓
                libc / IPC / devices
                         ↓
                  native X server
                         ↓
                 startx → xclock
                         ↓
               eventual self-hosting
```

The project builds these interfaces to support real consumers rather than introducing application-specific kernel workarounds.

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

### Automated kernel tests

Run the automated regression suite (including kernel and userspace tests) with:

```bash
make test-qemu
```

`make run-tests` currently invokes the same QEMU test runner.

The automated runner boots a kernel built with MYOS_KERNEL_TESTS=1 and uses
QEMU's isa-debug-exit device to convert the kernel test result into a process
exit status.

A successful run prints:

```text
[test] Kernel test suite passed
[test] QEMU kernel tests passed
```

and returns exit status 0.

A kernel panic, failed regression expectation, or unexpected QEMU termination
returns a non-zero exit status.

The automated QEMU exit path is used by `make test-qemu` and its `make run-tests` alias. Normal interactive boots and physical-hardware images do not use the automated test-exit device.

### Boot-time configuration

Limine passes the kernel command line in `limine.conf` (and `limine-test.conf` for the test image). Supported bootstrap options include:

| Option | Values | Default |
| --- | --- | --- |
| `myos.boot` | `normal`, `test` | `normal` |
| `myos.loglevel` | `quiet`, `normal`, `verbose`, `debug` | `normal` |
| `myos.console.scale` | integer `1`–`4` | `1` |

Unsupported or malformed option values preserve the current setting or its default. The normal Limine menu includes `MyOS` (scale 1) and `MyOS large text` (scale 2). These are two profiles for the same kernel, not separate builds.

In QEMU, interactive `make run` and `make run-usb` permit guest-triggered reboot. Ctrl+Alt+Delete is interpreted by MyOS's normalized input subsystem and requests a machine reset. The automated test runner intentionally retains a controlled QEMU-exit policy.

### Writing the USB image to physical media

Build the desired image from the repository root. Both commands produce `build/myos-usb.img`:

```bash
make usb-image                 # Normal boot
make usb-image-diagnostics     # Runtime diagnostics and kernel tests
```

**Warning:** writing the image to a whole disk destroys its existing partition table and data. Device identifiers can change after reconnecting a drive. Identify and verify the target USB disk **every time**, and never select the workstation's system disk. The `USB_DISC` assignments below are examples: replace them with the actual whole-disk path before running `dd`.

#### macOS

Identify the USB disk, then assign its **whole-disk** path to a shell variable (replace `/dev/disk4` with the actual device):

```bash
diskutil list
USB_DISC=/dev/disk4
diskutil info "$USB_DISC"
```

Unmount the whole disk without ejecting it:

```bash
diskutil unmountDisk "$USB_DISC"
```

For faster raw-device access, derive the corresponding `/dev/rdiskN` path from `USB_DISC` and write the image:

```bash
USB_RAW_DISC="/dev/r${USB_DISC#/dev/}"
sudo dd \
    if=build/myos-usb.img \
    of="$USB_RAW_DISC" \
    bs=1048576
```

Flush outstanding writes and eject the disk:

```bash
sync
diskutil eject "$USB_DISC"
```

#### Arch Linux

Ensure the image-building utilities are installed, and build the desired image as shown above:

```bash
sudo pacman -S --needed gptfdisk mtools
make check-usb-tools
```

Identify the USB **whole disk**, then assign its path to `USB_DISC` (replace `/dev/sdX` with the actual device; do not use a partition such as `/dev/sdX1`):

```bash
lsblk -o NAME,PATH,SIZE,MODEL,TRAN,TYPE,MOUNTPOINTS
USB_DISC=/dev/sdX
lsblk -o NAME,PATH,SIZE,MODEL,TRAN,TYPE,MOUNTPOINTS "$USB_DISC"
```

Verify that `USB_DISC` refers to the intended removable drive, not the Arch system disk. If the drive has mounted partitions, unmount them before writing the image. This loop works for both `/dev/sdX1` and `/dev/nvmeXnYp1` partition naming:

```bash
lsblk -nrpo NAME,TYPE "$USB_DISC" | while read -r partition type; do
    if [ "$type" = part ] && findmnt -rn --source "$partition" > /dev/null; then
        sudo umount "$partition"
    fi
done
```

Write to the **whole disk** identified by `USB_DISC`, not to an individual partition:

```bash
sudo dd \
    if=build/myos-usb.img \
    of="$USB_DISC" \
    bs=4M \
    status=progress \
    conv=fsync
```

Flush writes and safely power off the USB drive before unplugging it:

```bash
sync
sudo udisksctl power-off -b "$USB_DISC"
```

If `udisksctl` is unavailable, install `udisks2` or use the desktop environment's safe-removal action after `dd` and `sync` finish.

Detailed toolchain notes are available in:

```text
docs/toolchain.md
```

## Hardware scope

Development primarily targets QEMU Q35 with one x86-64 CPU. The scheduler, physical allocator, interrupt infrastructure, and other shared kernel state currently rely on this single-core assumption.

As of 2026-09-13, MyOS has also booted successfully on a physical Dell Xeon workstation from a UEFI USB image. The full runtime diagnostics and kernel test suite passed on that machine, including userspace execution, timer-driven scheduling, process isolation, paging hardening checks, and exception containment.

This is a validated physical-hardware milestone, not a claim of broad hardware compatibility. ACPI/MADT-based discovery is now implemented, but the current interactive 0.3.0 userland and the broader hardware/driver matrix still need further on-machine validation.

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

# MyOS Toolchain

MyOS is developed on an ARM64 macOS host and targets a freestanding x86-64 ELF environment.

The build environment is intentionally explicit. The project must not depend accidentally on the compiler or linker provided by macOS.

## Host

Current development host:

- macOS on Apple Silicon
- architecture: ARM64

The host architecture and the target architecture are different:

```text
Host:
ARM64 macOS

Target:
x86-64 freestanding ELF
```

This means the project is cross-compiled from the beginning.

## Compiler

MyOS uses upstream LLVM/Clang installed through Homebrew:

```text
LLVM 21
```

The compiler is resolved from:

```text
$(brew --prefix llvm@21)/bin/clang
```

The Apple-provided compiler located at:

```text
/usr/bin/clang
```

is intentionally not used for the MyOS build.

Compilation targets:

```text
x86_64-unknown-none-elf
```

This tells Clang to generate:

- x86-64 machine code;
- ELF object files;
- no dependency on a hosted operating system environment.

Kernel code will be compiled as freestanding C.

## Linker

MyOS uses LLVM LLD 21:

```text
$(brew --prefix lld@21)/bin/ld.lld
```

The macOS linker:

```text
/usr/bin/ld
```

is not used.

The reason is that MyOS uses ELF, while macOS normally uses Mach-O.

## LLVM inspection tools

The following LLVM tools are used to inspect generated artifacts:

```text
llvm-readelf
llvm-objdump
llvm-nm
```

These tools allow us to inspect:

- ELF headers;
- sections;
- program headers;
- symbols;
- generated machine code.

They are part of the learning and debugging workflow, not only build utilities.

## Emulator

MyOS initially runs under:

```text
qemu-system-x86_64
```

QEMU provides the x86-64 virtual machine in which the kernel executes.

The initial configuration will use:

- one virtual CPU;
- UEFI firmware;
- x86-64 emulation;
- no dependency on host CPU architecture.

## Debugger

The primary debugger is GNU GDB.

GDB will communicate with the QEMU gdbstub through a remote debugging connection.

Conceptually:

```text
GDB
 |
 | remote protocol
 v
QEMU
 |
 v
virtual x86-64 CPU
```

GDB does not directly execute or attach to the MyOS kernel as a native macOS process.

## Boot image tooling

`xorriso` is used to create ISO images containing:

- the MyOS kernel;
- bootloader files;
- boot configuration;
- UEFI boot files.

It is a host-side build tool and does not execute inside MyOS.

## Toolchain verification

The development environment can be checked with:

```bash
make check-toolchain
```

The command verifies that the required tools exist and prints their versions.

The build must fail early when a required tool is missing.

## Verified cross-compilation

The toolchain has been verified by compiling a freestanding C translation unit using:

```text
--target=x86_64-unknown-none-elf
```

The resulting object file was confirmed to have:

```text
Class:   ELF64
Machine: Advanced Micro Devices X86-64
Type:    REL
```

This demonstrates that an ARM64 macOS host can generate x86-64 ELF object files for MyOS.

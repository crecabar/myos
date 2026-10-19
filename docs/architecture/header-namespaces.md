# MyOS Header Namespaces and Module Boundaries

## Purpose

MyOS uses canonical header include paths to express ownership and
dependencies between components independently of the physical
location of source files.

This policy applies to kernel code, shared ABI definitions,
bootstrap userspace runtime, applications, and tests.

These are C preprocessor include namespaces, not language-level
namespaces.

## Namespace ownership

| Namespace | Include root | Ownership |
|---|---|---|
| `<myos/kernel/...>` | `kernel/include/` | Kernel-internal interfaces |
| `<myos/abi/...>` | `include/` | Shared kernel/userspace ABI |
| `<myos/...>` | `user/runtime/include/` | MyOS runtime extensions |
| `<sh/...>` | `user/bootstrap/bin/sh/include/` | Shell-private interfaces |
| `<init/...>` | `user/bootstrap/init/include/` | Init-private interfaces |

Standard C library interfaces retain their standard names,
such as `<stddef.h>`, `<stdint.h>`, `<stdio.h>`, and `<string.h>`.

The runtime include root is provisional while MyOS libc remains
in the bootstrap userspace tree.

## Kernel interfaces

Headers consumed across kernel module boundaries belong under
`kernel/include/myos/kernel/`.

The logical path should reflect the owning subsystem.

Example:

    kernel/include/myos/kernel/core/panic.h
    kernel/include/myos/kernel/core/execution/kernel_context.h
    kernel/include/myos/kernel/arch/x86_64/kernel_switch.h

Consumers use:

    #include <myos/kernel/core/panic.h>
    #include <myos/kernel/core/execution/kernel_context.h>
    #include <myos/kernel/arch/x86_64/kernel_switch.h>

Implementation source files remain within their existing owning
modules unless a separate architectural change justifies moving them.

A header used exclusively within one implementation directory may
remain local and be included using its basename in double quotes.

## Shared ABI

The shared ABI resides under `include/myos/abi/`.

These headers define intentional contracts between the kernel
and userspace.

ABI headers must not depend on kernel-internal implementation
headers or application-private interfaces.

Changing include paths does not authorize changes to syscall
numbers, binary structure layouts, calling conventions, or
other existing ABI contracts.

## Userspace runtime

Standard library declarations retain their conventional C header
names.

MyOS-specific runtime extensions use `<myos/...>`.

Examples:

    #include <stdio.h>
    #include <string.h>
    #include <myos/file.h>
    #include <myos/process.h>

Runtime-private headers may remain adjacent to implementation files.

The bootstrap runtime must not include kernel-internal headers.

## Application interfaces

Applications own their private include roots.

The shell already uses `<sh/...>`.

Init will use `<init/...>`.

Application-private include roots must only be supplied to their
own build targets and corresponding tests.

Single-file programs do not require empty include directories.

## Test interfaces

Kernel test infrastructure and contracts shared by test fixtures
must be separated from production interfaces.

Proposed test-only namespaces:

    <myos/kernel/tests/...>
    <myos/test/...>

The kernel test namespace owns test harness interfaces and
kernel-specific test infrastructure.

The shared test namespace owns test-only contracts consumed
by both kernel and userspace test fixtures.

Their precise physical include roots will be finalized during
the ABI and test-contract migration.

Production builds must not expose test-only include roots.

Tests may consume kernel-internal interfaces when exercising
those components, but must use canonical paths.

Test-only syscall constants are not part of the public ABI.

## Include conventions

1. Use canonical angle-bracket paths for cross-module dependencies.
2. Preserve double-quoted includes only for genuinely local,
   same-directory private headers.
3. Do not use `../` paths across module boundaries.
4. Do not introduce a repository-wide `-I.`.
5. Do not duplicate headers or retain compatibility forwarding
   headers solely to preserve obsolete include paths.
6. Prefer grouping the owning interface, other project interfaces,
   and standard C headers separately.
7. Keep include guards consistent with canonical header paths.
8. Do not modify functional contracts as part of a header move.

## Build boundaries

Include roots are target-specific.

The kernel consumes its internal interfaces and the shared ABI.

Bootstrap userspace consumes its runtime interfaces and the shared
ABI, but not kernel-internal interfaces.

Application-private include roots are limited to their owning
targets and corresponding tests.

Test-only roots are enabled only for the relevant test builds.

MyOS retains its modular `mk/*.mk` build organization.

## Migration policy

Header migration is incremental and mechanical.

Each checkpoint should:

1. Identify the header's ownership and all consumers.
2. Establish one canonical destination.
3. Move the existing header without changing its declarations.
4. Update all consumers, including nested headers and assembly.
5. Update affected build dependencies and include roots.
6. Validate compilation and relevant regressions.
7. Review the diff before committing.

Source files remain in their current modules by default.

The migration must not change runtime behavior.

## Validation

The refactor is complete when:

- All cross-module kernel includes use canonical paths.
- Shared ABI definitions remain accessible to both kernel
  and userspace without exposing kernel internals.
- Production userspace cannot resolve kernel-internal or
  test-only headers.
- Application-private include boundaries are maintained.
- Header dependency tracking works for incremental builds.
- Clean kernel and bootstrap userspace builds pass.
- Host tests and QEMU regressions pass.
- No functional behavior or ABI contracts have changed.

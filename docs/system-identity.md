# MyOS System Identity Contract

## Purpose

Define how MyOS exposes operating-system identity to userspace without duplicating kernel-owned metadata in individual executables.

This contract supports the initial `uname` utility and future libc consumers.

## Ownership

The running kernel is the authoritative source for operating-system identity.

Userspace applications must not embed the current kernel release as a substitute for querying the running system.

The public syscall ABI remains independent of the kernel release number.

## Identity fields

| Field | Meaning | Initial source |
|---|---|---|
| `sysname` | Operating-system name | `MYOS_NAME` |
| `release` | Running kernel release | `MYOS_VERSION` |
| `machine` | Running kernel architecture | Architecture configuration |
| `nodename` | Configurable system node name | Not yet implemented |
| `version` | Kernel build identification | Not yet implemented |

Initially supported values:

- `sysname`: `MyOS`
- `release`: `0.2.0`
- `machine`: `x86_64`

The values above describe the current implementation; they are not permanent ABI constants.

## Proposed native interface

The initial native interface uses SYSCALL_UNAME (number 17) and struct syscall_utsname, with five fixed-capacity, NUL-terminated fields of 65 bytes each. Fields not yet available are returned as empty strings. A successful query returns zero; invalid userspace destinations return the established negative syscall errors. The syscall is implemented in a subsequent slice.

## Initial syscall implementation

`SYSCALL_UNAME` accepts one userspace destination pointer to
`struct syscall_utsname` and returns zero on success.

The kernel constructs the complete structure in kernel-owned
memory and publishes it with `copy_to_user()`.

A destination that cannot hold the complete structure returns
`-SYSCALL_ERROR_BAD_ADDRESS` without partial publication.

The current kernel provides `sysname`, `release`, and `machine`.
`nodename` and `version` are empty NUL-terminated strings.

The initial implementation supports the x86_64 kernel target.

## Initial uname behavior

| Invocation | Output |
|---|---|
| `uname` | System name |
| `uname -s` | System name |
| `uname -r` | Kernel release |
| `uname -m` | Kernel architecture |
| `uname -a` | All identity fields supported by the initial contract |

Each successful invocation writes a newline-terminated result to stdout and exits with status zero.

Unsupported options must produce a nonzero exit status.

The exact format of `uname -a` will be established before implementation, without manufacturing unavailable nodename or build metadata.

## Testing requirements

The Ring-3 regression suite must validate exact stdout bytes and process exit status for each supported invocation.

Kernel tests must validate identity consistency, output-buffer boundaries, bad userspace addresses, and error behavior.

Kernel release and ABI version must remain independently versioned.

## Non-goals

This initial contract does not introduce a hostname configuration service, Linux compatibility, CPU feature enumeration, `/proc`, or persistent machine identity.

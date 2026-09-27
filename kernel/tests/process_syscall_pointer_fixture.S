// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_syscall_pointer_fixture.S
 * @brief Embeds the Ring-3 hostile syscall-pointer regression ELF.
 */

.section .rodata
.balign 16

.global process_syscall_pointer_fixture_start
.type process_syscall_pointer_fixture_start, @object

process_syscall_pointer_fixture_start:
    .incbin "build/user-tests/syscall_pointer.elf"

.global process_syscall_pointer_fixture_end
.type process_syscall_pointer_fixture_end, @object

process_syscall_pointer_fixture_end:

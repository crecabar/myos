// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process_fd_syscalls_fixture.S
 * @brief Embeds the Ring-3 descriptor syscall regression ELF.
 */

.section .rodata
.balign 16

.global process_fd_syscalls_fixture_start
.type process_fd_syscalls_fixture_start, @object

process_fd_syscalls_fixture_start:
    .incbin "build/user-tests/fd_syscalls.elf"

.global process_fd_syscalls_fixture_end
.type process_fd_syscalls_fixture_end, @object

process_fd_syscalls_fixture_end:

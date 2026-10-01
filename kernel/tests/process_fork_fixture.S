// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process_fork_fixture.S
 * @brief Embeds the Ring-3 copy-based fork regression ELF.
 */

.section .rodata
.balign 16

.global process_fork_fixture_start
.type process_fork_fixture_start, @object

process_fork_fixture_start:
    .incbin "build/user-tests/fork.elf"

.global process_fork_fixture_end
.type process_fork_fixture_end, @object

process_fork_fixture_end:

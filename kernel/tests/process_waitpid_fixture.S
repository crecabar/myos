// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process_waitpid_fixture.S
 * @brief Embeds the Ring-3 waitpid parent and child regression ELFs.
 */

.section .rodata
.balign 16

.global process_waitpid_parent_fixture_start
.type process_waitpid_parent_fixture_start, @object

process_waitpid_parent_fixture_start:
    .incbin "build/user-tests/waitpid_parent.elf"

.global process_waitpid_parent_fixture_end
.type process_waitpid_parent_fixture_end, @object

process_waitpid_parent_fixture_end:

.balign 16

.global process_waitpid_child_fixture_start
.type process_waitpid_child_fixture_start, @object

process_waitpid_child_fixture_start:
    .incbin "build/user-tests/waitpid_child.elf"

.global process_waitpid_child_fixture_end
.type process_waitpid_child_fixture_end, @object

process_waitpid_child_fixture_end:

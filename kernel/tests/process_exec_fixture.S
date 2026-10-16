// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_exec_fixture.S
 * @brief Embeds the Ring-3 exec caller and replacement ELF images.
 */

.section .rodata
.balign 16

.global process_exec_caller_fixture_start
.type process_exec_caller_fixture_start, @object

process_exec_caller_fixture_start:
    .incbin "build/user-tests/exec_caller.elf"

.global process_exec_caller_fixture_end
.type process_exec_caller_fixture_end, @object

process_exec_caller_fixture_end:

.balign 16

.global process_exec_target_fixture_start
.type process_exec_target_fixture_start, @object

process_exec_target_fixture_start:
    .incbin "build/user-tests/exec_target.elf"

.global process_exec_target_fixture_end
.type process_exec_target_fixture_end, @object

process_exec_target_fixture_end:

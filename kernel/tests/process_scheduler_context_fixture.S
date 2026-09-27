// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_scheduler_context_fixture.S
 * @brief Embeds the Ring-3 scheduler/context-switch regression ELF.
 */

.section .rodata
.balign 16

.global process_scheduler_context_fixture_start
.type process_scheduler_context_fixture_start, @object

process_scheduler_context_fixture_start:
    .incbin "build/user-tests/scheduler_context.elf"

.global process_scheduler_context_fixture_end
.type process_scheduler_context_fixture_end, @object

process_scheduler_context_fixture_end:

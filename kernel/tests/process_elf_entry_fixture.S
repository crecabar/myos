// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_elf_entry_fixture.S
 * @brief Embeds the ELF64 userspace entry test image in the test kernel.
 */

.section .rodata
.balign 16

.global process_elf_entry_fixture_start
.type process_elf_entry_fixture_start, @object

process_elf_entry_fixture_start:
    .incbin "build/user-tests/elf_entry.elf"

.global process_elf_entry_fixture_end
.type process_elf_entry_fixture_end, @object

process_elf_entry_fixture_end:

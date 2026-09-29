// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_elf_protection_fixture.S
 * @brief Embeds the hostile ELF text-protection regression image.
 */

.section .rodata
.balign 16

.global process_elf_protection_fixture_start
.type process_elf_protection_fixture_start, @object

process_elf_protection_fixture_start:
    .incbin "build/user-tests/elf_self_modify.elf"

.global process_elf_protection_fixture_end
.type process_elf_protection_fixture_end, @object

process_elf_protection_fixture_end:

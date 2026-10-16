// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process_elf_isolation_fixture.S
 * @brief Embeds cross-address-space isolation ELF fixtures.
 */

.section .rodata
.balign 16

.global process_elf_isolation_attacker_fixture_start
.type process_elf_isolation_attacker_fixture_start, @object

process_elf_isolation_attacker_fixture_start:
    .incbin "build/user-tests/elf_isolation_attacker.elf"

.global process_elf_isolation_attacker_fixture_end
.type process_elf_isolation_attacker_fixture_end, @object

process_elf_isolation_attacker_fixture_end:


.balign 16

.global process_elf_isolation_victim_fixture_start
.type process_elf_isolation_victim_fixture_start, @object

process_elf_isolation_victim_fixture_start:
    .incbin "build/user-tests/elf_isolation_victim.elf"

.global process_elf_isolation_victim_fixture_end
.type process_elf_isolation_victim_fixture_end, @object

process_elf_isolation_victim_fixture_end:

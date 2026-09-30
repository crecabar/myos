/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Cristian Recabarren
 */


/**
 * @file elf_isolation_attacker.ld
 * @brief Layout for the cross-address-space isolation attacker.
 */

ENTRY(_start)

PHDRS
{
    data PT_LOAD FLAGS(6);
    text PT_LOAD FLAGS(5);
}

SECTIONS
{
    /*
     * Deliberately use the victim's executable virtual address for private
     * attacker data.
     */
    . = 0x0000000000400000;

    .data :
    {
        *(.data)
        *(.data.*)
    } :data

    /*
     * Attacker instructions execute from a different RX page.
     */
    . = 0x0000000000500000;

    .text :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } :text

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}

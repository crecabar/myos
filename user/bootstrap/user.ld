/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file user.ld
 * @brief Linker script for bootstrap MyOS userspace executables.
 */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0000000000400000;

    .text : ALIGN(0x1000)
    {
        *(.text._start)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } :text

    . = ALIGN(0x1000);

    .data :
    {
        *(.data)
        *(.data.*)
    } :data

    .bss (NOLOAD) :
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.llvm_addrsig)
    }
}

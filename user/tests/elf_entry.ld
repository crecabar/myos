/* SPDX-License-Identifier: GPL-2.0-only */

/**
 * @file elf_entry.ld
 * @brief Linker script for the ELF64 userspace entry test program.
 */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
}

SECTIONS
{
    . = 0x0000000000400000;

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

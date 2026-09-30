// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */


/**
 * @file elf_self_modify.S
 * @brief Hostile Ring-3 ELF that attempts to modify its own RX text.
 */

.section .text
.global _start
.type _start, @function

_start:
    /*
     * Resolve the virtual address of our own executable entry point.
     *
     * The ELF loader must map this page as user-readable/executable and
     * non-writable. The following store must therefore fault in hardware.
     */
    leaq _start(%rip), %rbx

    /*
     * Attempt to replace the first instruction byte with NOP.
     *
     * Reaching the instruction after this store would mean W^X/RO text
     * protection has failed.
     */
    movb $0x90, (%rbx)

    /*
     * FAILURE PATH.
     *
     * The process must never reach this exit. If it does, the kernel test
     * harness will observe normal exit instead of a segmentation fault.
     */
    movq $2, %rax
    movq $99, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */


/**
 * @file waitpid_child.S
 * @brief Ring-3 child workload for the waitpid regression.
 */

.section .text
.global _start
.type _start, @function

.set SYSCALL_EXIT,  2
.set SYSCALL_YIELD, 3

_start:
    /*
     * Give a READY parent an opportunity to reach its blocking wait even if
     * timer scheduling happened to run this child earlier than expected.
     */
    movq $SYSCALL_YIELD, %rax
    int $0x80

    movq $SYSCALL_EXIT, %rax
    movq $37, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start

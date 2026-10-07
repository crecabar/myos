// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file syscall.S
 * @brief x86-64 bootstrap userspace syscall invocation primitive.
 */

.section .text

.global __myos_syscall6
.type __myos_syscall6, @function

/*
 * System V x86-64 function entry:
 *
 *     RDI       number
 *     RSI       argument0
 *     RDX       argument1
 *     RCX       argument2
 *     R8        argument3
 *     R9        argument4
 *     8(RSP)    argument5
 *
 * MyOS syscall ABI:
 *
 *     RAX       number
 *     RDI       argument0
 *     RSI       argument1
 *     RDX       argument2
 *     R10       argument3
 *     R8        argument4
 *     R9        argument5
 *
 * The kernel returns the raw syscall result in RAX.
 */
__myos_syscall6:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
    movq 8(%rsp), %r9

    int $0x80

    ret

.size __myos_syscall6, . - __myos_syscall6

.section .note.GNU-stack,"",@progbits

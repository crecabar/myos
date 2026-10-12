// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

.section .text

.global irq_supervisor_enter
.type irq_supervisor_enter, @function

/*
 * RDI = supervisor stack top
 * RSI = supervisor entry function
 * RDX = callback argument
 *
 * Preconditions:
 *   - CPL 0
 *   - IF = 0
 *   - stack_top aligned to 16 bytes
 *   - callback never returns
 *
 * This function abandons the caller's stack.
 */
irq_supervisor_enter:
    /*
     * Establish the independent supervisor stack.
     */
    movq %rdi, %rsp

    /*
     * The previous frame chain belongs to the
     * abandoned execution stack.
     */
    xorq %rbp, %rbp

    /*
     * Transfer the callback argument to RDI.
     */
    movq %rdx, %rdi

    /*
     * Preserve the System V AMD64 call convention.
     *
     * RSP is 16-byte aligned before CALL and the
     * callee receives RSP mod 16 == 8.
     */
    cld
    call *%rsi

    /*
     * A supervisor returning here violates the
     * non-returning trampoline contract.
     */
    ud2

.size irq_supervisor_enter, .-irq_supervisor_enter

.section .note.GNU-stack,"",@progbits

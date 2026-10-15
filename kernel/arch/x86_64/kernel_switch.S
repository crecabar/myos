// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

.section .text

.global kernel_context_switch
.type kernel_context_switch, @function

/*
 * RDI = current context
 * RSI = next context
 *
 * Saves the current call continuation, then resumes
 * the target using its saved RSP and return address.
 */
kernel_context_switch:
    movq %rsp, 0(%rdi)
    movq %rbx, 8(%rdi)
    movq %rbp, 16(%rdi)
    movq %r12, 24(%rdi)
    movq %r13, 32(%rdi)
    movq %r14, 40(%rdi)
    movq %r15, 48(%rdi)

    movq 8(%rsi), %rbx
    movq 16(%rsi), %rbp
    movq 24(%rsi), %r12
    movq 32(%rsi), %r13
    movq 40(%rsi), %r14
    movq 48(%rsi), %r15
    movq 0(%rsi), %rsp

    ret

.size kernel_context_switch, .-kernel_context_switch

.global kernel_supervisor_stack_call
.type kernel_supervisor_stack_call, @function

/*
 * RDI = dedicated stack top
 * RSI = callback
 * RDX = callback argument
 *
 * This is a synchronous Ring 0 stack transition.
 * It is not an IRQ-return mechanism.
 */
kernel_supervisor_stack_call:
    pushq %r12
    movq %rsp, %r12

    /*
     * Switch to the dedicated supervisor stack.
     * stack_top is required to be 16-byte aligned.
     */
    movq %rdi, %rsp

    /*
     * First callback argument.
     */
    movq %rdx, %rdi

    call *%rsi

    /*
     * Restore the original supervisor caller stack.
     */
    movq %r12, %rsp
    popq %r12
    ret

.size kernel_supervisor_stack_call, .-kernel_supervisor_stack_call

.section .note.GNU-stack,"",@progbits

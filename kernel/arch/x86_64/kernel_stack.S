// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

.section .text

.global kernel_stack_call
.type kernel_stack_call, @function

/*
 * System V AMD64 ABI:
 *
 * RDI = alternative stack top
 * RSI = entry callback
 * RDX = callback argument
 *
 * Preconditions are enforced by the caller.
 */
kernel_stack_call:
    /*
     * Preserve RBX, a callee-saved register.
     * Save the original stack pointer after the push.
     */
    pushq %rbx
    movq %rsp, %rbx

    /*
     * Switch to the alternative stack.
     *
     * stack_top is 16-byte aligned. The subsequent CALL
     * establishes the expected 8-byte offset on entry.
     */
    movq %rdi, %rsp

    /*
     * Invoke entry(argument).
     */
    movq %rsi, %rax
    movq %rdx, %rdi
    call *%rax

    /*
     * Restore the original stack and RBX.
     */
    movq %rbx, %rsp
    popq %rbx
    ret

.size kernel_stack_call, .-kernel_stack_call

.section .note.GNU-stack,"",@progbits

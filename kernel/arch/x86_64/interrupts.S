// SPDX-License-Identifier: GPL-2.0-only

.section .text

.global isr_divide_error
.type isr_divide_error, @function

.extern divide_error_handler

isr_divide_error:
    /*
     * CPU already pushed:
     *
     *   RIP
     *   CS
     *   RFLAGS
     *
     * Save general-purpose registers.
     */

    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp

    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /*
     * RSP now points to struct interrupt_context.
     */
    movq %rsp, %rdi

    cld

    /*
     * Handler is fatal for now, so we do not need to restore
     * the original stack afterwards.
     */
    andq $-16, %rsp

    call divide_error_handler

1:
    hlt
    jmp 1b

.size isr_divide_error, . - isr_divide_error

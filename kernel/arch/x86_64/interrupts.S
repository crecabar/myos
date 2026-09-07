// SPDX-License-Identifier: GPL-2.0-only

.section .text

.global isr_divide_error
.type isr_divide_error, @function

.extern exception_handler

isr_divide_error:
    /*
     * #DE does not push an error code.
     *
     * Normalize the frame:
     *   vector = 0
     *   error_code = 0
     */
    pushq $0
    pushq $0

    jmp isr_common

.global isr_page_fault
.type isr_page_fault, @function

isr_page_fault:
    /*
     * #PF already pushes an error code.
     *
     * CPU stack on entry:
     *
     *   error_code
     *   RIP
     *   CS
     *   RFLAGS
     *
     * We only need to prepend the vector.
     */
    pushq $14
    jmp isr_common

.size isr_page_fault, . - isr_page_fault

isr_common:
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
     * Fatal handler for now: no iretq path yet.
     */
    andq $-16, %rsp

    call exception_handler

1:
    hlt
    jmp 1b

.size isr_divide_error, . - isr_divide_error

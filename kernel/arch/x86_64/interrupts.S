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

.extern syscall_handler

.global isr_syscall
.type isr_syscall, @function

isr_syscall:
    pushq $0
    pushq $0x80

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

    movq %rsp, %rdi

    subq $8, %rsp
    call syscall_handler
    addq $8, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp

    iretq

.size isr_syscall, .-isr_syscall

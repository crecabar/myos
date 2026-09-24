// SPDX-License-Identifier: GPL-2.0-only

.section .text

.global usermode_enter_asm
.type usermode_enter_asm, @function

usermode_enter_asm:
    cli

    mov %cx, %ax
    mov %ax, %ds
    mov %ax, %es

    pushq %rcx
    pushq %rsi

    pushfq
    popq %rax
    orq $0x2, %rax
    orq $0x200, %rax
    pushq %rax

    pushq %rdx
    pushq %rdi

    iretq

.size usermode_enter_asm, .-usermode_enter_asm


/*
 * usermode_resume_asm:
 *
 * RDI = pointer to struct interrupt_context
 * RSI = user data-segment selector
 *
 * The supplied frame follows the same layout consumed by the
 * existing interrupt-return paths in interrupts.S.
 *
 * This function does not return to its C caller.
 */
.global usermode_resume_asm
.type usermode_resume_asm, @function

usermode_resume_asm:
    cli

    /*
     * Prepare the user data segments before discarding the
     * current kernel call stack.
     */
    movw %si, %ax
    movw %ax, %ds
    movw %ax, %es

    /*
     * Continue using the prepared interrupt frame as the
     * stack. The frame remains valid until IRETQ consumes it.
     */
    movq %rdi, %rsp

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

    /*
     * Discard vector and error_code. IRETQ then restores
     * RIP, CS, RFLAGS, RSP and SS.
     */
    addq $16, %rsp

    iretq

.size usermode_resume_asm, .-usermode_resume_asm

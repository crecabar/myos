// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

.section .text

.extern kernel_stack_call

.global kernel_stack_abi_test
.type kernel_stack_abi_test, @function

/*
 * bool kernel_stack_abi_test(
 *     void *stack_top,
 *     void (*entry)(void *),
 *     void *argument
 * );
 *
 * Returns 1 if all callee-saved registers survive
 * kernel_stack_call(), otherwise returns 0.
 *
 * The caller must disable maskable interrupts.
 */
kernel_stack_abi_test:
    /*
     * Preserve the caller's callee-saved registers.
     * Six pushes keep RSP aligned to 16 bytes before CALL.
     */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movabsq $0x1122334455667788, %rbx
    movabsq $0x2233445566778899, %rbp
    movabsq $0x33445566778899AA, %r12
    movabsq $0x445566778899AABB, %r13
    movabsq $0x5566778899AABBCC, %r14
    movabsq $0x66778899AABBCCDD, %r15

    call kernel_stack_call

    movl $1, %eax

    movabsq $0x1122334455667788, %rcx
    cmpq %rcx, %rbx
    jne .Labi_failed

    movabsq $0x2233445566778899, %rcx
    cmpq %rcx, %rbp
    jne .Labi_failed

    movabsq $0x33445566778899AA, %rcx
    cmpq %rcx, %r12
    jne .Labi_failed

    movabsq $0x445566778899AABB, %rcx
    cmpq %rcx, %r13
    jne .Labi_failed

    movabsq $0x5566778899AABBCC, %rcx
    cmpq %rcx, %r14
    jne .Labi_failed

    movabsq $0x66778899AABBCCDD, %rcx
    cmpq %rcx, %r15
    jne .Labi_failed

    jmp .Labi_done

.Labi_failed:
    xorl %eax, %eax

.Labi_done:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.size kernel_stack_abi_test, .-kernel_stack_abi_test

.section .note.GNU-stack,"",@progbits

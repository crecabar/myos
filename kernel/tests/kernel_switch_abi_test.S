// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

.section .text

.extern kernel_context_switch

.global kernel_switch_abi_test
.type kernel_switch_abi_test, @function

/*
 * bool kernel_switch_abi_test(
 *     struct kernel_switch_context *current,
 *     const struct kernel_switch_context *next
 * );
 *
 * Returns true when the original context is resumed
 * with its callee-saved registers intact.
 */
kernel_switch_abi_test:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /*
     * Six pushes preserve 16-byte call-site alignment.
     */
    movabsq $0x1122334455667788, %rbx
    movabsq $0x2233445566778899, %rbp
    movabsq $0x33445566778899AA, %r12
    movabsq $0x445566778899AABB, %r13
    movabsq $0x5566778899AABBCC, %r14
    movabsq $0x66778899AABBCCDD, %r15

    call kernel_context_switch

    movl $1, %eax

    movabsq $0x1122334455667788, %rcx
    cmpq %rcx, %rbx
    jne .Lswitch_abi_failed

    movabsq $0x2233445566778899, %rcx
    cmpq %rcx, %rbp
    jne .Lswitch_abi_failed

    movabsq $0x33445566778899AA, %rcx
    cmpq %rcx, %r12
    jne .Lswitch_abi_failed

    movabsq $0x445566778899AABB, %rcx
    cmpq %rcx, %r13
    jne .Lswitch_abi_failed

    movabsq $0x5566778899AABBCC, %rcx
    cmpq %rcx, %r14
    jne .Lswitch_abi_failed

    movabsq $0x66778899AABBCCDD, %rcx
    cmpq %rcx, %r15
    jne .Lswitch_abi_failed

    jmp .Lswitch_abi_done

.Lswitch_abi_failed:
    xorl %eax, %eax

.Lswitch_abi_done:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.size kernel_switch_abi_test, .-kernel_switch_abi_test

.section .note.GNU-stack,"",@progbits

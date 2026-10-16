// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file block_probe.S
 * @brief Raw Ring-3 explicit BLOCKED/resume regression program.
 */

#include "../../../syscall/test_numbers.h"

.section .rodata
.balign 16

.global user_program_block_probe_start
.type user_program_block_probe_start, @object

.set SYSCALL_EXIT, 2

user_program_block_probe_start:
    /*
     * RBX must survive the explicit block/resume transition.
     */
    movq $43, %rbx

    movq $SYSCALL_TEST_BLOCK, %rax
    int $0x80

    /*
     * Successful resumption returns RAX == 0.
     */
    testq %rax, %rax
    setz %al

    /*
     * RBX must still contain its userspace sentinel.
     */
    cmpq $43, %rbx
    sete %cl

    andb %cl, %al

    /*
     * Exit 43 when both checks succeeded, otherwise 42.
     */
    movzbl %al, %edi
    addq $42, %rdi

    movl $SYSCALL_EXIT, %eax
    int $0x80

.Lhang:
    jmp .Lhang

.global user_program_block_probe_end
.type user_program_block_probe_end, @object

user_program_block_probe_end:

.size user_program_block_probe_start, \
    user_program_block_probe_end - user_program_block_probe_start

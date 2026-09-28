// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file exec_caller.S
 * @brief Ring-3 exec image-replacement regression caller.
 */
 #include "../../kernel/syscall/test_numbers.h"

.section .text
.global _start
.type _start, @function

.set SYSCALL_EXIT,      2

_start:
    /*
     * First exercise the failure path. The kernel deliberately attempts an
     * invalid replacement and must return INVALID_ARGUMENT while leaving this
     * executable image alive.
     */
    movq $SYSCALL_TEST_EXEC, %rax
    xorq %rdi, %rdi
    int $0x80

    cmpq $-1, %rax
    jne .Lfail

    /*
     * A successful exec must never return to the instruction following this
     * interrupt. IRETQ must instead enter the replacement ELF at its _start.
     */
    movq $SYSCALL_TEST_EXEC, %rax
    movq $1, %rdi
    int $0x80

.Lfail:
    movq $SYSCALL_EXIT, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file sleep_probe.S
 * @brief Raw Ring-3 sleep/resume regression program.
 */

#include "../../../syscall/test_numbers.h"

.section .rodata
.balign 16

.global user_program_sleep_probe_start
.type user_program_sleep_probe_start, @object

.set SYSCALL_EXIT, 2

user_program_sleep_probe_start:
    /*
     * Preserve RBX across the syscall/context-switch path.
     */
    movq $42, %rbx

    /*
     * sleep(0) must return zero immediately.
     */
    movl $SYSCALL_TEST_SLEEP, %eax
    xorl %edi, %edi
    int $0x80

    testq %rax, %rax
    sete %r8b

    /*
     * sleep(UINT64_MAX) must reject the duration with -1.
     */
    movl $SYSCALL_TEST_SLEEP, %eax
    movq $-1, %rdi
    int $0x80

    cmpq $-1, %rax
    sete %r9b
    andb %r9b, %r8b

    /*
     * sleep(50) must suspend and later resume successfully.
     */
    movl $SYSCALL_TEST_SLEEP, %eax
    movl $50, %edi
    int $0x80

    testq %rax, %rax
    sete %r9b
    andb %r9b, %r8b

    /*
     * RBX must survive the syscall and context switch.
     */
    cmpq $42, %rbx
    sete %r9b
    andb %r9b, %r8b

    /*
     * Exit 42 when every check succeeded, otherwise 41.
     */
    movzbl %r8b, %edi
    addl $41, %edi

    movl $SYSCALL_EXIT, %eax
    int $0x80

.Lhang:
    jmp .Lhang

.global user_program_sleep_probe_end
.type user_program_sleep_probe_end, @object

user_program_sleep_probe_end:

.size user_program_sleep_probe_start, \
    user_program_sleep_probe_end - user_program_sleep_probe_start

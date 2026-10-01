// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */


/**
 * @file waitpid_parent.S
 * @brief Ring-3 waitpid blocking and reaping regression parent.
 *
 * The kernel test harness supplies the direct child PID in RDI before this
 * process executes.
 *
 * Published ABI values are intentionally hard-coded here so the fixture acts
 * as an independent userspace consumer.
 */

.section .text
.global _start
.type _start, @function

.set SYSCALL_EXIT,      2
.set SYSCALL_WAITPID,   5

.set WAITPID_NOHANG,    1

.set ERROR_BAD_ADDRESS, -2
.set ERROR_NO_CHILD,    -4

.set WAIT_EXITED,       1

_start:
    /*
     * Reserve the 16-byte syscall_wait_status result.
     */
    subq $16, %rsp

    /*
     * Preserve the child PID independently of syscall argument registers.
     */
    movq %rdi, %r12

    testq %r12, %r12
    jz .Lfail

    /*
     * The child exists but has not terminated yet.
     *
     * waitpid(child, NULL, NOHANG) must therefore return zero.
     */
    movq $SYSCALL_WAITPID, %rax
    movq %r12, %rdi
    xorq %rsi, %rsi
    movq $WAITPID_NOHANG, %rdx
    int $0x80

    testq %rax, %rax
    jne .Lfail

    /*
     * Enter the real blocking path with a deliberately invalid status
     * pointer. The first invocation blocks while the child is alive.
     *
     * After child termination, the kernel retries this same int 0x80.
     * The userspace copy must then fail with BAD_ADDRESS without reaping
     * the child.
     */
    movq $SYSCALL_WAITPID, %rax
    movq %r12, %rdi
    movq $1, %rsi
    xorq %rdx, %rdx
    int $0x80

    cmpq $ERROR_BAD_ADDRESS, %rax
    jne .Lfail

    /*
     * The child must still be waitable after the failed status copy.
     */
    movq $0, 0(%rsp)
    movq $0, 8(%rsp)

    /*
     * Consume any terminated direct child. This exercises wait()-style
     * semantics through waitpid(0, ...).
     */
    movq $SYSCALL_WAITPID, %rax
    xorq %rdi, %rdi
    movq %rsp, %rsi
    xorq %rdx, %rdx
    int $0x80

    cmpq %r12, %rax
    jne .Lfail

    /*
     * struct syscall_wait_status:
     *
     *   +0  termination_reason
     *   +8  exit_status
     */
    cmpq $WAIT_EXITED, 0(%rsp)
    jne .Lfail

    cmpq $37, 8(%rsp)
    jne .Lfail

    /*
     * Reaping is exactly-once. A second wait for the same PID must report
     * that it is no longer a direct child.
     */
    movq $SYSCALL_WAITPID, %rax
    movq %r12, %rdi
    movq %rsp, %rsi
    xorq %rdx, %rdx
    int $0x80

    cmpq $ERROR_NO_CHILD, %rax
    jne .Lfail

    addq $16, %rsp

    movq $SYSCALL_EXIT, %rax
    xorq %rdi, %rdi
    int $0x80

.Lfail:
    addq $16, %rsp

    movq $SYSCALL_EXIT, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start

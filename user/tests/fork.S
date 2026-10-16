// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */


/**
 * @file fork.S
 * @brief Ring-3 copy-based fork regression.
 *
 * Published syscall ABI values are intentionally hard-coded so this fixture
 * behaves as an independent userspace consumer.
 */

.section .text
.global _start
.type _start, @function

.set SYSCALL_EXIT,      2
.set SYSCALL_YIELD,     3
.set SYSCALL_WAITPID,   5
.set SYSCALL_FORK,      6

.set ERROR_NO_CHILD,    -4

.set WAIT_EXITED,       1

.set PARENT_SENTINEL,   0x1122334455667788
.set CHILD_SENTINEL,    0x8877665544332211

_start:
    /*
     * Layout:
     *
     *   +0   private forked memory probe
     *   +8   wait termination_reason
     *   +16  wait exit_status
     *   +24  spare/alignment
     */
    subq $32, %rsp

    movabsq $PARENT_SENTINEL, %rax
    movq %rax, 0(%rsp)

    /*
     * Both processes resume immediately after this instruction.
     *
     * Parent: RAX = child PID
     * Child:  RAX = 0
     */
    movq $SYSCALL_FORK, %rax
    int $0x80

    testq %rax, %rax
    jz .Lchild

    js .Lfail

    /*
     * Parent path.
     */
    movq %rax, %r12

    testq %r12, %r12
    jz .Lfail

    movq $0, 8(%rsp)
    movq $0, 16(%rsp)

    /*
     * This should block until the forked child exits.
     */
    movq $SYSCALL_WAITPID, %rax
    movq %r12, %rdi
    leaq 8(%rsp), %rsi
    xorq %rdx, %rdx
    int $0x80

    cmpq %r12, %rax
    jne .Lfail

    cmpq $WAIT_EXITED, 8(%rsp)
    jne .Lfail

    cmpq $42, 16(%rsp)
    jne .Lfail

    /*
     * The child wrote another value to exactly the same userspace VA.
     * The parent's frame must remain unchanged.
     */
    movabsq $PARENT_SENTINEL, %rax
    cmpq %rax, 0(%rsp)
    jne .Lfail

    /*
     * The child must have been reaped exactly once.
     */
    movq $SYSCALL_WAITPID, %rax
    movq %r12, %rdi
    leaq 8(%rsp), %rsi
    xorq %rdx, %rdx
    int $0x80

    cmpq $ERROR_NO_CHILD, %rax
    jne .Lfail

    addq $32, %rsp

    movq $SYSCALL_EXIT, %rax
    xorq %rdi, %rdi
    int $0x80

.Lchild:
    /*
     * If the child was scheduled before the parent reached waitpid(),
     * yield once so the parent can enter the blocking wait.
     *
     * If the parent is already BLOCKED, this process may simply be selected
     * again and continue normally.
     */
    movq $SYSCALL_YIELD, %rax
    int $0x80

    /*
     * We resumed from exactly the same instruction and stack pointer as the
     * parent, but this write must touch only the child's private frame.
     */
    movabsq $CHILD_SENTINEL, %rax
    movq %rax, 0(%rsp)

    /*
     * Read it back locally as a basic sanity check.
     */
    cmpq %rax, 0(%rsp)
    jne .Lchild_fail

    addq $32, %rsp

    movq $SYSCALL_EXIT, %rax
    movq $42, %rdi
    int $0x80

.Lchild_fail:
    addq $32, %rsp

    movq $SYSCALL_EXIT, %rax
    movq $41, %rdi
    int $0x80

.Lfail:
    addq $32, %rsp

    movq $SYSCALL_EXIT, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start

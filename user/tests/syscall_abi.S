// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file syscall_abi.S
 * @brief Ring-3 regression program for the MyOS syscall ABI.
 *
 * This fixture intentionally hard-codes the published ABI values instead
 * of including kernel-private headers. It therefore behaves like an
 * independent userspace ABI consumer.
 */

.section .text
.global _start
.type _start, @function

.set SYSCALL_EXIT,                  2
.set UNKNOWN_SYSCALL,     0x7fffffff
.set ERROR_NOT_IMPLEMENTED,        -3

_start:
    /*
     * Preserve the original process stack pointer in userspace memory.
     *
     * After this push, the expected RSP across int 0x80 is:
     *
     *   original RSP - 8
     */
    movq %rsp, %rax
    pushq %rax

    /*
     * Establish recognizable userspace flag state.
     *
     * CF and DF must survive the complete syscall round trip.
     * IF is already set by the normal MyOS userspace context.
     */
    stc
    std

    /*
     * Every general-purpose register other than RAX receives a distinct
     * sentinel. The syscall entry/return path must preserve all of them.
     */
    movq $0x11, %rbx
    movq $0x22, %rcx
    movq $0x33, %rdx
    movq $0x44, %rsi
    movq $0x55, %rdi
    movq $0x66, %rbp

    movq $0x77, %r8
    movq $0x88, %r9
    movq $0x99, %r10
    movq $0xaa, %r11
    movq $0xbb, %r12
    movq $0xcc, %r13
    movq $0xdd, %r14
    movq $0xee, %r15

    /*
     * Invoke an intentionally unsupported syscall.
     *
     * Expected result:
     *
     *   RAX = -SYSCALL_ERROR_NOT_IMPLEMENTED = -3
     */
    movq $UNKNOWN_SYSCALL, %rax
    int $0x80

    /*
     * Capture returned RFLAGS before any comparison modifies them.
     *
     * Keep the syscall result temporarily on the stack so RAX can be
     * used to inspect the returned flags.
     */
    pushq %rax
    pushfq
    popq %rax

    /*
     * CF must have survived.
     */
    testq $0x1, %rax
    jz .Lfail

    /*
     * IF must still be enabled in the returned userspace context.
     */
    testq $0x200, %rax
    jz .Lfail

    /*
     * DF must be restored to the userspace value even though the kernel
     * cleared it before entering C.
     */
    testq $0x400, %rax
    jz .Lfail

    /*
     * Restore the syscall result.
     */
    popq %rax

    cmpq $ERROR_NOT_IMPLEMENTED, %rax
    jne .Lfail

    cmpq $0x11, %rbx
    jne .Lfail

    cmpq $0x22, %rcx
    jne .Lfail

    cmpq $0x33, %rdx
    jne .Lfail

    cmpq $0x44, %rsi
    jne .Lfail

    cmpq $0x55, %rdi
    jne .Lfail

    cmpq $0x66, %rbp
    jne .Lfail

    cmpq $0x77, %r8
    jne .Lfail

    cmpq $0x88, %r9
    jne .Lfail

    cmpq $0x99, %r10
    jne .Lfail

    cmpq $0xaa, %r11
    jne .Lfail

    cmpq $0xbb, %r12
    jne .Lfail

    cmpq $0xcc, %r13
    jne .Lfail

    cmpq $0xdd, %r14
    jne .Lfail

    cmpq $0xee, %r15
    jne .Lfail

    /*
     * Verify RSP.
     *
     * The value stored at [RSP] is the original entry stack pointer.
     * The current value must therefore be exactly eight bytes lower.
     */
    movq (%rsp), %rax
    subq $8, %rax
    cmpq %rax, %rsp
    jne .Lfail

    /*
     * Do not leave DF set for the explicit exit request. The kernel entry
     * is robust to it, but the fixture itself has finished testing DF.
     */
    cld

    /*
     * Restore the original entry stack pointer.
     */
    addq $8, %rsp

    /*
     * exit(0): ABI contract passed.
     */
    movq $SYSCALL_EXIT, %rax
    xorq %rdi, %rdi
    int $0x80

.Lfail:
    /*
     * exit(1): some ABI invariant failed.
     */
    cld

    movq $SYSCALL_EXIT, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start

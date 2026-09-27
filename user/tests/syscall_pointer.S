// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file syscall_pointer.S
 * @brief Ring-3 regression program for hostile syscall user pointers.
 *
 * This fixture intentionally consumes the published syscall ABI directly.
 * Every rejected WRITE must return normally to CPL3 with BAD_ADDRESS.
 */

.section .text
.global _start
.type _start, @function

.set SYSCALL_EXIT,         2
.set SYSCALL_WRITE,        4

.set ERROR_BAD_ADDRESS,   -2

.set REGISTER_SENTINEL,    0x5a5a5a5a

_start:
    /*
     * Unrelated process state must survive every rejected syscall.
     */
    movq $REGISTER_SENTINEL, %rbx

    /*
     * NULL with non-zero length.
     */
    xorq %rdi, %rdi
    movq $1, %rsi
    call .Lexpect_bad_address

    /*
     * Near-null pointer below PROCESS_USER_VIRTUAL_MIN.
     */
    movq $1, %rdi
    movq $1, %rsi
    call .Lexpect_bad_address

    /*
     * Lowest legal userspace address, deliberately unmapped.
     */
    movq $0x1000, %rdi
    movq $1, %rsi
    call .Lexpect_bad_address

    /*
     * Canonical higher-half address.
     */
    movabsq $0xffff800000000000, %rdi
    movq $1, %rsi
    call .Lexpect_bad_address

    /*
     * Range begins inside legal userspace but extends past
     * PROCESS_USER_VIRTUAL_MAX.
     */
    movabsq $0x00007ffffffffff8, %rdi
    movq $16, %rsi
    call .Lexpect_bad_address

    /*
     * Arithmetic-overflow-shaped pointer.
     *
     * A naive UINT64_MAX - 7 + 16 calculation would wrap.
     */
    movq $-8, %rdi
    movq $16, %rsi
    call .Lexpect_bad_address

    /*
     * Cross from the mapped top stack page into the unmapped page
     * immediately above it.
     *
     * The initial RSP is inside the highest mapped stack page, so
     * rounding it upward gives the exclusive stack top.
     */
    movq %rsp, %r12
    addq $0xfff, %r12
    andq $-4096, %r12
    subq $8, %r12

    movq %r12, %rdi
    movq $16, %rsi
    call .Lexpect_bad_address

    /*
     * Finally prove that a legitimate buffer spanning two mapped stack
     * pages still succeeds after all rejected calls.
     *
     * R12 = eight bytes before the current stack page boundary.
     */
    movq %rsp, %r12
    andq $-4096, %r12
    subq $8, %r12

    /*
     * "MULTIPAGE-OK!!!\n"
     */
    movabsq $0x47415049544c554d, %rax
    movq %rax, (%r12)

    movabsq $0x0a2121214b4f2d45, %rax
    movq %rax, 8(%r12)

    movq $SYSCALL_WRITE, %rax
    movq %r12, %rdi
    movq $16, %rsi
    int $0x80

    cmpq $16, %rax
    jne .Lfail

    cmpq $REGISTER_SENTINEL, %rbx
    jne .Lfail

    /*
     * All hostile pointers were rejected and the valid multi-page
     * operation still worked.
     */
    movq $SYSCALL_EXIT, %rax
    xorq %rdi, %rdi
    int $0x80

.Lexpect_bad_address:
    movq $SYSCALL_WRITE, %rax
    int $0x80

    cmpq $ERROR_BAD_ADDRESS, %rax
    jne .Lfail

    cmpq $REGISTER_SENTINEL, %rbx
    jne .Lfail

    ret

.Lfail:
    movq $SYSCALL_EXIT, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start

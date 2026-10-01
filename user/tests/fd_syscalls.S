// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file fd_syscalls.S
 * @brief Ring-3 regression program for descriptor file syscalls.
 *
 * This fixture intentionally consumes the published MyOS syscall ABI
 * independently of kernel headers.
 *
 * The initial regression covers:
 *
 *   open("/file", READ | WRITE)
 *   fstat(fd)
 *   close(fd)
 *   close(fd) -> BAD_DESCRIPTOR
 *   open("/missing", READ) -> NOT_FOUND
 */

.section .text
.global _start
.type _start, @function

/*
 * Published MyOS syscall ABI v1 numbers.
 */
.set SYSCALL_EXIT,       2
.set SYSCALL_FD_OPEN,    7
.set SYSCALL_FD_CLOSE,   8
.set SYSCALL_FD_FSTAT,  12

/*
 * Published open access flags.
 */
.set OPEN_ACCESS_READ,   1
.set OPEN_ACCESS_WRITE,  2

/*
 * Published file types.
 */
.set FILE_TYPE_REGULAR_FILE, 1

/*
 * Published syscall errors are returned negated.
 */
.set ERROR_BAD_DESCRIPTOR,  -6
.set ERROR_NOT_FOUND,       -7

/*
 * The kernel fixture exposes /file with this initial size.
 */
.set EXPECTED_FILE_SIZE,    16

_start:
    /*
     * Open /file read/write.
     *
     * RDI = pathname address
     * RSI = pathname length
     * RDX = access flags
     *
     * The pathname deliberately has no trailing NUL in its ABI length.
     */
    movq $SYSCALL_FD_OPEN, %rax
    leaq .Lfile_path(%rip), %rdi
    movq $(.Lfile_path_end - .Lfile_path), %rsi
    movq $(OPEN_ACCESS_READ | OPEN_ACCESS_WRITE), %rdx
    int $0x80

    /*
     * Any negative value is a syscall error.
     */
    testq %rax, %rax
    js .Lfail

    /*
     * Keep the descriptor in a preserved general-purpose register.
     * The syscall ABI promises every GPR except RAX survives returning
     * syscalls.
     */
    movq %rax, %r12

    /*
     * Reserve exactly one syscall_file_stat:
     *
     *   +0  uint64_t type
     *   +8  uint64_t size
     */
    subq $16, %rsp

    movq $SYSCALL_FD_FSTAT, %rax
    movq %r12, %rdi
    movq %rsp, %rsi
    int $0x80

    testq %rax, %rax
    jne .Lfstat_fail

    cmpq $FILE_TYPE_REGULAR_FILE, 0(%rsp)
    jne .Lfstat_fail

    cmpq $EXPECTED_FILE_SIZE, 8(%rsp)
    jne .Lfstat_fail

    addq $16, %rsp

    /*
     * Close the live descriptor.
     */
    movq $SYSCALL_FD_CLOSE, %rax
    movq %r12, %rdi
    int $0x80

    testq %rax, %rax
    jne .Lfail

    /*
     * Closing the same descriptor again must report BAD_DESCRIPTOR.
     */
    movq $SYSCALL_FD_CLOSE, %rax
    movq %r12, %rdi
    int $0x80

    cmpq $ERROR_BAD_DESCRIPTOR, %rax
    jne .Lfail

    /*
     * A missing pathname must report NOT_FOUND.
     */
    movq $SYSCALL_FD_OPEN, %rax
    leaq .Lmissing_path(%rip), %rdi
    movq $(.Lmissing_path_end - .Lmissing_path), %rsi
    movq $OPEN_ACCESS_READ, %rdx
    int $0x80

    cmpq $ERROR_NOT_FOUND, %rax
    jne .Lfail

    /*
     * Every descriptor syscall behaved according to the published ABI.
     */
    movq $SYSCALL_EXIT, %rax
    xorq %rdi, %rdi
    int $0x80

.Lfstat_fail:
    /*
     * Restore the stack before reporting failure.
     */
    addq $16, %rsp

.Lfail:
    movq $SYSCALL_EXIT, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start


.section .rodata

/*
 * Pathnames are explicit byte sequences. Their syscall lengths exclude any
 * NUL terminator; none is required by the MyOS pathname ABI.
 */
.Lfile_path:
    .ascii "/file"
.Lfile_path_end:

.Lmissing_path:
    .ascii "/missing"
.Lmissing_path_end:

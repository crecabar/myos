// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file elf_entry.S
 * @brief Minimal ELF64 userspace entry program for kernel tests.
 */

.section .text
.global _start
.type _start, @function

_start:
    /*
     * The minimal MyOS process-entry ABI places argc at [rsp].
     *
     * This program is launched with two arguments by the kernel test. Reading
     * argc here proves that the ELF entry point received the stack built by
     * process_stack_build_initial().
     */
    cmpq $2, (%rsp)
    jne .Lbad_stack

    /*
     * write(message, message_length)
     *
     * RAX = SYSCALL_WRITE = 4
     * RDI = message
     * RSI = message length
     */
    movq $4, %rax
    leaq message(%rip), %rdi
    movq $(message_end - message), %rsi
    int $0x80

    /*
     * exit(0)
     *
     * RAX = SYSCALL_EXIT = 2
     * RDI = status
     */
    movq $2, %rax
    xorq %rdi, %rdi
    int $0x80

.Lbad_stack:
    /*
     * exit(1)
     */
    movq $2, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

message:
    .ascii "ELF _start OK\n"
message_end:

.size _start, . - _start

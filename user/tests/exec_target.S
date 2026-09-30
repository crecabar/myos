// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */


/**
 * @file exec_target.S
 * @brief Replacement ELF for the Ring-3 exec regression.
 */

.section .text
.global _start
.type _start, @function

.set SYSCALL_EXIT,  2
.set SYSCALL_WRITE, 4

_start:
    /*
     * Expected replacement stack:
     *
     *   argc = 2
     *   argv[0] = "exec-target"
     *   argv[1] = "replacement"
     *   envp[0] = "EXEC=1"
     */
    cmpq $2, (%rsp)
    jne .Lfail

    movq 8(%rsp), %rax

    cmpb $'e', 0(%rax)
    jne .Lfail
    cmpb $'x', 1(%rax)
    jne .Lfail
    cmpb $'e', 2(%rax)
    jne .Lfail
    cmpb $'c', 3(%rax)
    jne .Lfail
    cmpb $'-', 4(%rax)
    jne .Lfail
    cmpb $'t', 5(%rax)
    jne .Lfail
    cmpb $'a', 6(%rax)
    jne .Lfail
    cmpb $'r', 7(%rax)
    jne .Lfail
    cmpb $'g', 8(%rax)
    jne .Lfail
    cmpb $'e', 9(%rax)
    jne .Lfail
    cmpb $'t', 10(%rax)
    jne .Lfail
    cmpb $0, 11(%rax)
    jne .Lfail

    movq 16(%rsp), %rax

    cmpb $'r', 0(%rax)
    jne .Lfail
    cmpb $'e', 1(%rax)
    jne .Lfail
    cmpb $'p', 2(%rax)
    jne .Lfail
    cmpb $'l', 3(%rax)
    jne .Lfail
    cmpb $'a', 4(%rax)
    jne .Lfail
    cmpb $'c', 5(%rax)
    jne .Lfail
    cmpb $'e', 6(%rax)
    jne .Lfail
    cmpb $'m', 7(%rax)
    jne .Lfail
    cmpb $'e', 8(%rax)
    jne .Lfail
    cmpb $'n', 9(%rax)
    jne .Lfail
    cmpb $'t', 10(%rax)
    jne .Lfail
    cmpb $0, 11(%rax)
    jne .Lfail

    cmpq $0, 24(%rsp)
    jne .Lfail

    movq 32(%rsp), %rax

    cmpb $'E', 0(%rax)
    jne .Lfail
    cmpb $'X', 1(%rax)
    jne .Lfail
    cmpb $'E', 2(%rax)
    jne .Lfail
    cmpb $'C', 3(%rax)
    jne .Lfail
    cmpb $'=', 4(%rax)
    jne .Lfail
    cmpb $'1', 5(%rax)
    jne .Lfail
    cmpb $0, 6(%rax)
    jne .Lfail

    cmpq $0, 40(%rsp)
    jne .Lfail

    movq $SYSCALL_WRITE, %rax
    leaq .Lmessage(%rip), %rdi
    movq $(.Lmessage_end - .Lmessage), %rsi
    int $0x80

    cmpq $(.Lmessage_end - .Lmessage), %rax
    jne .Lfail

    movq $SYSCALL_EXIT, %rax
    xorq %rdi, %rdi
    int $0x80

.Lfail:
    movq $SYSCALL_EXIT, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start

.section .rodata

.Lmessage:
    .ascii "EXEC-TARGET-OK\n"

.Lmessage_end:

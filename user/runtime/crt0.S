// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file crt0.S
 * @brief Minimal C runtime entry point for bootstrap userspace programs.
 */

.section .text._start

.global _start
.type _start, @function

.extern main

.set SYSCALL_EXIT, 2

_start:
    /*
     * Initial MyOS userspace stack:
     *
     *     rsp + 0                  argc
     *     rsp + 8                  argv[0]
     *     ...
     *                              argv[argc - 1]
     *                              NULL
     *                              envp[0]
     *     ...
     *                              NULL
     *
     * Convert that representation into the x86-64 System V C calling
     * convention:
     *
     *     RDI = argc
     *     RSI = argv
     *     RDX = envp
     */
    movq (%rsp), %rdi
    leaq 8(%rsp), %rsi
    leaq 16(%rsp,%rdi,8), %rdx

    call main

    /*
     * A returning main() terminates the process using its int return value.
     *
     * The syscall number is part of MyOS syscall ABI version 1. A public
     * userspace syscall header will replace this local definition as the
     * bootstrap runtime grows under #57.
     */
    movslq %eax, %rdi
    movq $SYSCALL_EXIT, %rax
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start

.section .note.GNU-stack,"",@progbits

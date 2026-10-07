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
.extern _exit

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
     * Returning from main terminates the process through the bootstrap
     * runtime. crt0 itself deliberately knows nothing about the architecture
     * syscall instruction or the numeric exit syscall identifier.
     */
    movl %eax, %edi
    call _exit

.Lhang:
    jmp .Lhang

.size _start, . - _start

.section .note.GNU-stack,"",@progbits

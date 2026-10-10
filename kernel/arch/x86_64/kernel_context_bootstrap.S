// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

.section .text

.extern kernel_context_trampoline

.global kernel_context_bootstrap
.type kernel_context_bootstrap, @function

/*
 * Entered through RET from kernel_context_switch().
 *
 * R12 contains the current struct kernel_context pointer.
 * On entry, RSP % 16 == 8.
 */
kernel_context_bootstrap:
    movq %r12, %rdi

    /*
     * Tail-transfer to C, preserving the ABI's
     * expected function-entry stack alignment.
     *
     * The C trampoline must never return.
     */
    jmp kernel_context_trampoline

.size kernel_context_bootstrap, .-kernel_context_bootstrap

.section .note.GNU-stack,"",@progbits

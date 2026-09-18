// SPDX-License-Identifier: GPL-2.0-only

.section .text

.global arch_stack_enter
.type arch_stack_enter, @function

arch_stack_enter:
    movq %rdi, %rsp
    andq $-16, %rsp

    /*
     * Do not retain a frame-chain reference into the abandoned boot stack.
     */
    xorq %rbp, %rbp

    /*
     * The call pushes an 8-byte return address, so the entry function sees
     * the System V AMD64 stack alignment expected by compiled C code.
     */
    call *%rsi

    /*
     * The entry function is required not to return.
     */
    ud2

.size arch_stack_enter, .-arch_stack_enter

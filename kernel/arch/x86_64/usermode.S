// SPDX-License-Identifier: GPL-2.0-only

.section .text

.global usermode_enter_asm
.type usermode_enter_asm, @function

usermode_enter_asm:
    cli

    mov %cx, %ax
    mov %ax, %ds
    mov %ax, %es

    pushq %rcx
    pushq %rsi

    pushfq
    popq %rax
    orq $0x2, %rax
    orq $0x200, %rax
    pushq %rax

    pushq %rdx
    pushq %rdi

    iretq

.size usermode_enter_asm, .-usermode_enter_asm

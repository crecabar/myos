// SPDX-License-Identifier: GPL-2.0-only

.section .text

.global gdt_load
.type gdt_load, @function

gdt_load:
    cli

    movzwq %si, %rsi
    movzwq %dx, %rdx

    lgdt (%rdi)

    mov %dx, %ds
    mov %dx, %es
    mov %dx, %ss

    pushq %rsi
    leaq .Lgdt_loaded(%rip), %rax
    pushq %rax
    lretq

.Lgdt_loaded:
    ret

.size gdt_load, .-gdt_load

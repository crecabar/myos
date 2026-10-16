// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file elf_from_vfs.S
 * @brief Userspace ELF fixture loaded from the VFS-backed initramfs.
 */

.section .text
.global _start
.type _start, @function

.set SYSCALL_EXIT,  2
.set SYSCALL_WRITE, 4

_start:
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
    .ascii "ELF-FROM-VFS-OK\n"

.Lmessage_end:

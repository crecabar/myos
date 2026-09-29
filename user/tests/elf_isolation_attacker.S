// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file elf_isolation_attacker.S
 * @brief Ring-3 cross-address-space isolation attacker.
 */

.section .data

.global attacker_target
.type attacker_target, @object

attacker_target:
    .byte 0x00

.size attacker_target, . - attacker_target


.section .text

.global _start
.type _start, @function

_start:
    /*
     * Another process maps executable text at this exact virtual address.
     *
     * In this process, however, 0x400000 belongs to our private RW data
     * segment. A correct address-space implementation must resolve this
     * write exclusively through our own CR3.
     */
    movabsq $0x0000000000400000, %rbx
    movb $0xA5, (%rbx)

    cmpb $0xA5, (%rbx)
    jne .Lfail

    movq $2, %rax
    xorq %rdi, %rdi
    int $0x80

.Lfail:
    movq $2, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start

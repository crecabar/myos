// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file elf_isolation_victim.S
 * @brief Ring-3 victim for cross-address-space isolation regression.
 */

.section .text

.global victim_marker
.type victim_marker, @object

victim_marker:
    .byte 0x5A

.size victim_marker, . - victim_marker


.global _start
.type _start, @function

_start:
    /*
     * Yield several times. Even if this process happens to run before the
     * attacker, the attacker gets an opportunity to complete first.
     */
    movq $3, %rax
    int $0x80

    movq $3, %rax
    int $0x80

    movq $3, %rax
    int $0x80

    movq $3, %rax
    int $0x80

    /*
     * The attacker wrote 0xA5 to virtual address 0x400000 in its own address
     * space. Our executable byte at the same VA must remain 0x5A.
     */
    cmpb $0x5A, victim_marker(%rip)
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

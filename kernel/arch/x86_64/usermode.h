// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file usermode.h
 * @brief x86-64 transition from kernel mode to user mode.
 */

#ifndef MYOS_ARCH_X86_64_USERMODE_H
#define MYOS_ARCH_X86_64_USERMODE_H

#include <stdint.h>

/**
 * Enters x86-64 user mode at the requested instruction and stack pointers.
 *
 * The caller must activate an address space containing valid user-accessible
 * mappings for both RIP and RSP before calling this function.
 *
 * This function performs a privilege transition from ring 0 to ring 3 using
 * IRETQ and does not return.
 *
 * @param instruction_pointer Initial user-mode RIP.
 * @param stack_pointer Initial user-mode RSP.
 */
_Noreturn void usermode_enter(
    uint64_t instruction_pointer,
    uint64_t stack_pointer
);

#endif

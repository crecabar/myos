// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file stack.h
 * @brief x86-64 stack transition helpers.
 */

#ifndef MYOS_ARCH_X86_64_STACK_H
#define MYOS_ARCH_X86_64_STACK_H

#include <stdint.h>

typedef void (*arch_stack_entry)(void);

/**
 * Abandons the current stack and enters a function on a new stack.
 *
 * The supplied stack address is aligned down to the System V AMD64 ABI
 * boundary before the entry function is called. This function does not
 * return to the previous stack.
 *
 * @param stack_top Top address of the new downward-growing stack.
 * @param entry Function to enter after switching stacks.
 */
_Noreturn void arch_stack_enter(
    uint64_t stack_top,
    arch_stack_entry entry
);

#endif

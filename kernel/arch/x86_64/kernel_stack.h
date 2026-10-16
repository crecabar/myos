// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_ARCH_X86_64_KERNEL_STACK_H
#define MYOS_ARCH_X86_64_KERNEL_STACK_H

typedef void (*kernel_stack_entry_fn)(void *argument);

/**
 * Calls entry(argument) on an alternative kernel stack.
 *
 * Preconditions:
 * - Running in Ring 0.
 * - Maskable interrupts disabled throughout the call.
 * - stack_top is the exclusive upper bound of a mapped,
 *   writable, 16-byte-aligned stack region.
 * - The alternative stack does not overlap the current stack.
 * - entry returns normally and does not enable interrupts,
 *   block, yield, or switch address spaces.
 *
 * Restores the original stack pointer before returning.
 *
 * This is not a schedulable context switch.
 */
void kernel_stack_call(
    void *stack_top,
    kernel_stack_entry_fn entry,
    void *argument
);

#endif

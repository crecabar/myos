// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_KERNEL_ARCH_X86_64_KERNEL_SWITCH_H
#define MYOS_KERNEL_ARCH_X86_64_KERNEL_SWITCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Cooperative Ring 0 execution context.
 *
 * RSP identifies a suspended call stack whose next
 * instruction is reached through RET.
 *
 * This is not an interrupt frame.
 */
struct kernel_switch_context {
    uint64_t rsp;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};

_Static_assert(
    sizeof(struct kernel_switch_context) == 56,
    "Kernel switch context layout mismatch"
);

_Static_assert(
    offsetof(struct kernel_switch_context, rsp) == 0,
    "Kernel switch RSP offset mismatch"
);

_Static_assert(
    offsetof(struct kernel_switch_context, rbx) == 8,
    "Kernel switch RBX offset mismatch"
);

_Static_assert(
    offsetof(struct kernel_switch_context, rbp) == 16,
    "Kernel switch RBP offset mismatch"
);

_Static_assert(
    offsetof(struct kernel_switch_context, r12) == 24,
    "Kernel switch R12 offset mismatch"
);

_Static_assert(
    offsetof(struct kernel_switch_context, r13) == 32,
    "Kernel switch R13 offset mismatch"
);

_Static_assert(
    offsetof(struct kernel_switch_context, r14) == 40,
    "Kernel switch R14 offset mismatch"
);

_Static_assert(
    offsetof(struct kernel_switch_context, r15) == 48,
    "Kernel switch R15 offset mismatch"
);

/**
 * Saves the current cooperative context and resumes another.
 *
 * Both contexts must belong to the same CPU and kernel
 * address space. The target must already have a valid
 * resumable stack frame.
 *
 * Preconditions:
 * - Ring 0 execution.
 * - Maskable interrupts disabled.
 * - No concurrent access to either descriptor.
 * - No blocking or preemption inside the transition.
 *
 * Returns when a subsequent switch resumes the caller.
 */
void kernel_context_switch(
    struct kernel_switch_context *current,
    const struct kernel_switch_context *next
);

/**
 * Prepares an initial cooperative Ring 0 stack frame.
 *
 * entry must not return. The initial frame is consumed
 * by kernel_context_switch() through RET.
 *
 * No CPU context switch is performed.
 */
bool kernel_switch_prepare(
    struct kernel_switch_context *cpu,
    uintptr_t stack_bottom,
    uintptr_t stack_top,
    void (*entry)(void)
);

/**
 * Invokes a supervisor callback on a dedicated Ring 0 stack.
 *
 * stack_top must be 16-byte aligned and refer to a writable
 * kernel stack whose lifetime covers the entire callback.
 *
 * The caller must execute in Ring 0, outside IRQ context,
 * with interrupts disabled.
 *
 * The callback must return normally and must not switch
 * address spaces or retain pointers into its stack.
 *
 * On return, the caller's original stack is restored.
 */
void kernel_supervisor_stack_call(
    uintptr_t stack_top,
    void (*entry)(void *),
    void *argument
);

#endif

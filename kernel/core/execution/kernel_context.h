// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_CORE_EXECUTION_KERNEL_CONTEXT_H
#define MYOS_CORE_EXECUTION_KERNEL_CONTEXT_H

#include "../../arch/x86_64/kernel_switch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_CONTEXT_STACK_ALIGNMENT 16U
#define KERNEL_CONTEXT_MIN_STACK_SIZE 4096U
#define KERNEL_CONTEXT_NO_CPU UINT32_MAX

enum kernel_context_state {
    KERNEL_CONTEXT_UNINITIALIZED,
    KERNEL_CONTEXT_READY,
    KERNEL_CONTEXT_RUNNING,
    KERNEL_CONTEXT_WAITING,
    KERNEL_CONTEXT_TERMINATED,
};

typedef void (*kernel_context_entry_fn)(void *argument);

struct kernel_context {
    uintptr_t stack_bottom;
    uintptr_t stack_top;

    kernel_context_entry_fn entry;
    void *argument;

    struct kernel_switch_context cpu;
    struct kernel_switch_context *supervisor;

    enum kernel_context_state state;
    bool prepared;

    _Atomic uint32_t owner_cpu_id;
};

bool kernel_context_init(
    struct kernel_context *context,
    void *stack,
    size_t stack_size,
    kernel_context_entry_fn entry,
    void *argument
);

bool kernel_context_prepare(
    struct kernel_context *context,
    void (*trampoline)(void)
);

/**
 * Binds a prepared kernel context to its supervisor.
 *
 * The supervisor owns its CPU context storage and must keep it
 * alive until the worker terminates.
 *
 * Binding does not switch CPU contexts or change execution state.
 */
bool kernel_context_bind_supervisor(
    struct kernel_context *context,
    struct kernel_switch_context *supervisor
);

/**
 * Moves a prepared, supervised context from READY to RUNNING.
 * Does not perform a CPU context switch.
 */
bool kernel_context_start(struct kernel_context *context);

/**
 * Moves the running context to WAITING.
 * Does not suspend the CPU context by itself.
 */
bool kernel_context_wait(struct kernel_context *context);

/**
 * Suspends the running worker and returns control to its supervisor.
 *
 * On successful resumption, execution continues immediately after
 * kernel_context_switch(), and this function returns true.
 *
 * Requires execution on the worker's own stack, with interrupts
 * disabled and the supervisor's context still alive.
 *
 * This is a cooperative, single-CPU primitive.
 */
bool kernel_context_yield_wait(struct kernel_context *context);

/**
 * Makes a waiting context runnable again.
 */
bool kernel_context_wake(struct kernel_context *context);

/**
 * Marks a running context permanently terminated.
 */
bool kernel_context_terminate(struct kernel_context *context);

/**
 * Checks whether a stack pointer belongs to the context stack.
 *
 * This function does not determine whether the context is
 * currently executing.
 */
bool kernel_context_contains_stack_pointer(
    const struct kernel_context *context,
    uintptr_t stack_pointer
);

/**
 * Acquires exclusive ownership for a logical CPU.
 *
 * Does not change the lifecycle state or execute the worker.
 */
bool kernel_context_acquire(
    struct kernel_context *context,
    uint32_t logical_cpu_id
);

/**
 * Releases ownership held by the specified logical CPU.
 *
 * Must only be called after execution has returned safely
 * to the supervisor.
 */
bool kernel_context_release(
    struct kernel_context *context,
    uint32_t logical_cpu_id
);

/**
 * Returns the owner CPU ID, or KERNEL_CONTEXT_NO_CPU.
 */
uint32_t kernel_context_owner(
    const struct kernel_context *context
);

/**
 * Architecture bootstrap entry. Never returns.
 */
_Noreturn void kernel_context_bootstrap(void);

#endif

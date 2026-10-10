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
 * Makes a waiting context runnable again.
 */
bool kernel_context_wake(struct kernel_context *context);

/**
 * Marks a running context permanently terminated.
 */
bool kernel_context_terminate(struct kernel_context *context);

/**
 * Architecture bootstrap entry. Never returns.
 */
_Noreturn void kernel_context_bootstrap(void);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_CORE_EXECUTION_KERNEL_CONTEXT_H
#define MYOS_CORE_EXECUTION_KERNEL_CONTEXT_H

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
};

typedef void (*kernel_context_entry_fn)(void *argument);

struct kernel_context {
    uintptr_t stack_bottom;
    uintptr_t stack_top;

    kernel_context_entry_fn entry;
    void *argument;

    enum kernel_context_state state;
};

bool kernel_context_init(
    struct kernel_context *context,
    void *stack,
    size_t stack_size,
    kernel_context_entry_fn entry,
    void *argument
);

#endif

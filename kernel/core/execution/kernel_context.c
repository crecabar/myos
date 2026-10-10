// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_context.h"

#include <stddef.h>
#include <stdint.h>

bool kernel_context_init(
    struct kernel_context *context,
    void *stack,
    size_t stack_size,
    kernel_context_entry_fn entry,
    void *argument)
{
    if (
        context == NULL ||
        stack == NULL ||
        entry == NULL
    ) {
        return false;
    }

    uintptr_t bottom = (uintptr_t) stack;

    if (
        (bottom % KERNEL_CONTEXT_STACK_ALIGNMENT) != 0 ||
        stack_size < KERNEL_CONTEXT_MIN_STACK_SIZE ||
        (stack_size % KERNEL_CONTEXT_STACK_ALIGNMENT) != 0
    ) {
        return false;
    }

    if (stack_size > UINTPTR_MAX - bottom) {
        return false;
    }

    uintptr_t top = bottom + stack_size;

    *context = (struct kernel_context) {
        .stack_bottom = bottom,
        .stack_top = top,
        .entry = entry,
        .argument = argument,
        .state = KERNEL_CONTEXT_READY,
    };

    return true;
}

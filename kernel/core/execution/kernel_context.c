// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_context.h"

#include "../panic.h"

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
        .cpu = {0},
        .supervisor = NULL,
        .state = KERNEL_CONTEXT_READY,
        .prepared = false,
    };

    return true;
}

bool kernel_context_prepare(
    struct kernel_context *context,
    void (*trampoline)(void))
{
    if (
        context == NULL ||
        trampoline == NULL ||
        context->state != KERNEL_CONTEXT_READY ||
        context->prepared
    ) {
        return false;
    }

    if (!kernel_switch_prepare(
        &context->cpu,
        context->stack_bottom,
        context->stack_top,
        trampoline
    )) {
        return false;
    }

    /*
     * R12 transports the descriptor to the first-entry
     * trampoline. It is restored by kernel_context_switch().
     */
    context->cpu.r12 = (uint64_t) (uintptr_t) context;

    context->prepared = true;

    return true;
}

bool kernel_context_bind_supervisor(
    struct kernel_context *context,
    struct kernel_switch_context *supervisor)
{
    if (
        context == NULL ||
        supervisor == NULL ||
        !context->prepared ||
        context->state != KERNEL_CONTEXT_READY ||
        context->supervisor != NULL ||
        supervisor == &context->cpu
    ) {
        return false;
    }

    context->supervisor = supervisor;

    return true;
}

bool kernel_context_start(struct kernel_context *context)
{
    if (
        context == NULL ||
        context->state != KERNEL_CONTEXT_READY ||
        !context->prepared ||
        context->supervisor == NULL ||
        context->cpu.rsp == 0
    ) {
        return false;
    }

    context->state = KERNEL_CONTEXT_RUNNING;

    return true;
}

bool kernel_context_wait(struct kernel_context *context)
{
    if (
        context == NULL ||
        context->state != KERNEL_CONTEXT_RUNNING
    ) {
        return false;
    }

    context->state = KERNEL_CONTEXT_WAITING;

    return true;
}

bool kernel_context_yield_wait(struct kernel_context *context)
{
    if (
        context == NULL ||
        context->state != KERNEL_CONTEXT_RUNNING ||
        !context->prepared ||
        context->supervisor == NULL ||
        context->cpu.rsp == 0
    ) {
        return false;
    }

    uintptr_t current_rsp;

    __asm__ volatile (
        "movq %%rsp, %0"
        : "=r"(current_rsp)
        :
        : "memory"
    );

    /*
     * Only the worker executing on its own stack may
     * suspend this context.
     */
    if (!kernel_context_contains_stack_pointer(
        context,
        current_rsp
    )) {
        return false;
    }

    if (!kernel_context_wait(context)) {
        return false;
    }

    kernel_context_switch(
        &context->cpu,
        context->supervisor
    );

    /*
     * A resumed worker must have been explicitly
     * transitioned back to RUNNING.
     */
    if (context->state != KERNEL_CONTEXT_RUNNING) {
        kernel_panic("Kernel context resumed in invalid state");
    }

    return true;
}

bool kernel_context_wake(struct kernel_context *context)
{
    if (
        context == NULL ||
        context->state != KERNEL_CONTEXT_WAITING
    ) {
        return false;
    }

    context->state = KERNEL_CONTEXT_READY;

    return true;
}

bool kernel_context_terminate(struct kernel_context *context)
{
    if (
        context == NULL ||
        context->state != KERNEL_CONTEXT_RUNNING
    ) {
        return false;
    }

    context->state = KERNEL_CONTEXT_TERMINATED;

    return true;
}

bool kernel_context_contains_stack_pointer(
    const struct kernel_context *context,
    uintptr_t stack_pointer)
{
    if (
        context == NULL ||
        context->stack_bottom == 0 ||
        context->stack_top <= context->stack_bottom
    ) {
        return false;
    }

    return (
        stack_pointer >= context->stack_bottom &&
        stack_pointer < context->stack_top
    );
}

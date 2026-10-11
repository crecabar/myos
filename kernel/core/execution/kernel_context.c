// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_context.h"

#include "../panic.h"

#include <stdatomic.h>
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
        .owner_cpu_id = KERNEL_CONTEXT_NO_CPU,
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

    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    if ((rflags & (1ULL << 9)) != 0) {
        return false;
    }

    if (!kernel_context_wait(context)) {
        return false;
    }

    kernel_context_switch(
        &context->cpu,
        context->supervisor
    );

    uint64_t resumed_rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(resumed_rflags)
        :
        : "memory"
    );

    if ((resumed_rflags & (1ULL << 9)) != 0) {
        kernel_panic("Kernel context resumed with interrupts enabled");
    }

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

bool kernel_context_acquire(
    struct kernel_context *context,
    uint32_t logical_cpu_id)
{
    if (
        context == NULL ||
        logical_cpu_id == KERNEL_CONTEXT_NO_CPU ||
        !context->prepared ||
        context->supervisor == NULL ||
        context->state != KERNEL_CONTEXT_READY
    ) {
        return false;
    }

    uint32_t expected = KERNEL_CONTEXT_NO_CPU;

    return atomic_compare_exchange_strong_explicit(
        &context->owner_cpu_id,
        &expected,
        logical_cpu_id,
        memory_order_acquire,
        memory_order_relaxed
    );
}

bool kernel_context_release(
    struct kernel_context *context,
    uint32_t logical_cpu_id)
{
    if (
        context == NULL ||
        logical_cpu_id == KERNEL_CONTEXT_NO_CPU
    ) {
        return false;
    }

    uint32_t expected = logical_cpu_id;

    return atomic_compare_exchange_strong_explicit(
        &context->owner_cpu_id,
        &expected,
        KERNEL_CONTEXT_NO_CPU,
        memory_order_release,
        memory_order_relaxed
    );
}

uint32_t kernel_context_owner(
    const struct kernel_context *context)
{
    if (context == NULL) {
        return KERNEL_CONTEXT_NO_CPU;
    }

    return atomic_load_explicit(
        &context->owner_cpu_id,
        memory_order_acquire
    );
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <myos/kernel/core/execution/kernel_dispatch.h>

#include <myos/kernel/core/cpu/cpu_local.h>
#include "../panic.h"
#include "kernel_context.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define X86_RFLAGS_IF (1ULL << 9)

static bool kernel_dispatch_interrupts_disabled(void)
{
    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    return (rflags & X86_RFLAGS_IF) == 0;
}

bool kernel_context_dispatch(
    struct cpu_local *cpu,
    struct kernel_context *context)
{
    if (
        cpu == NULL ||
        !cpu->initialized ||
        !cpu_local_matches_current_cpu(cpu) ||
        context == NULL ||
        context->state != KERNEL_CONTEXT_READY ||
        !context->prepared ||
        context->supervisor == NULL ||
        context->cpu.rsp == 0 ||
        cpu_local_active_kernel_context(cpu) != NULL ||
        !kernel_dispatch_interrupts_disabled()
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
     * Dispatch must originate outside the worker stack.
     */
    if (kernel_context_contains_stack_pointer(
        context,
        current_rsp
    )) {
        return false;
    }

    /*
     * Acquire exclusive ownership before changing lifecycle
     * state or publishing the worker as active on this CPU.
     */
    if (!kernel_context_acquire(
        context,
        cpu->logical_id
    )) {
        return false;
    }

    if (!kernel_context_start(context)) {
        if (!kernel_context_release(
            context,
            cpu->logical_id
        )) {
            kernel_panic("Unable to roll back kernel context ownership");
        }

        return false;
    }

    /*
     * The CPU-local active reference is published before
     * switching to the worker's execution stack.
     */
    if (!cpu_local_activate_kernel_context(
        cpu,
        context
    )) {
        kernel_panic("Unable to activate dispatched kernel context");
    }

    kernel_context_switch(
        context->supervisor,
        &context->cpu
    );

    /*
     * We have returned physically to the supervisor.
     * The worker must no longer be RUNNING.
     */
    if (
        context->state != KERNEL_CONTEXT_WAITING &&
        context->state != KERNEL_CONTEXT_TERMINATED
    ) {
        kernel_panic("Kernel context returned in invalid state");
    }

    if (!kernel_dispatch_interrupts_disabled()) {
        kernel_panic("Kernel context returned with interrupts enabled");
    }

    if (
        kernel_context_owner(context) != cpu->logical_id ||
        cpu_local_active_kernel_context(cpu) != context
    ) {
        kernel_panic("Kernel context ownership lost during dispatch");
    }

    if (!cpu_local_deactivate_kernel_context(
        cpu,
        context
    )) {
        kernel_panic("Unable to deactivate dispatched kernel context");
    }

    /*
     * Release ownership only after the supervisor has
     * resumed and CPU-local active identity was removed.
     */
    if (!kernel_context_release(
        context,
        cpu->logical_id
    )) {
        kernel_panic("Unable to release dispatched kernel context");
    }

    /*
     * A work notification may have arrived while the
     * worker was RUNNING.
     *
     * Consume the latched request only after CPU ownership
     * has returned to the supervisor.
     */
    bool reschedule =
        atomic_exchange_explicit(
            &context->reschedule_requested,
            false,
            memory_order_acq_rel
        );

    if (
        reschedule &&
        context->state == KERNEL_CONTEXT_WAITING
    ) {
        if (!kernel_context_wake(context)) {
            kernel_panic(
                "Unable to rearm yielded kernel worker"
            );
        }
    }

    return true;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_dispatch.h"

#include "../cpu/cpu_local.h"
#include "../panic.h"
#include "kernel_context.h"

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

    if (!kernel_context_start(context)) {
        return false;
    }

    /*
     * The RUNNING descriptor becomes the logical target
     * before the physical context switch.
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
     * Execution has returned to the supervisor.
     * The worker must have yielded or terminated.
     */
    if (
        context->state != KERNEL_CONTEXT_WAITING &&
        context->state != KERNEL_CONTEXT_TERMINATED
    ) {
        kernel_panic("Kernel context returned in invalid state");
    }

    if (!cpu_local_deactivate_kernel_context(
        cpu,
        context
    )) {
        kernel_panic("Unable to deactivate dispatched kernel context");
    }

    return true;
}

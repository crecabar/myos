// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_context.h"

#include "../panic.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Called by the x86-64 bootstrap with the descriptor
 * passed through R12.
 *
 * The supervisor owns the stack and descriptor lifetime.
 */
_Noreturn void kernel_context_trampoline(
    struct kernel_context *context)
{
    if (
        context == NULL ||
        !context->prepared ||
        context->state != KERNEL_CONTEXT_RUNNING ||
        context->entry == NULL ||
        context->supervisor == NULL
    ) {
        kernel_panic(
            "Invalid kernel context trampoline entry"
        );
    }

    context->entry(context->argument);

    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    if ((rflags & (1ULL << 9)) != 0) {
        kernel_panic(
            "Kernel context terminated with interrupts enabled"
        );
    }

    /*
     * A normally returning entry completes the context.
     * It cannot be scheduled again.
     */
    if (!kernel_context_terminate(context)) {
        kernel_panic("Unable to terminate kernel context");
    }

    kernel_context_switch(
        &context->cpu,
        context->supervisor
    );

    /*
     * A terminated context must never be resumed.
     */
    kernel_panic(
        "Terminated kernel context resumed"
    );
}

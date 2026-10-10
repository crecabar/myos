// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_context_test.h"

#include "../core/execution/kernel_context.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>
#include <stdint.h>

static _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
unsigned char test_stack[KERNEL_CONTEXT_MIN_STACK_SIZE];

static void kernel_context_test_entry(void *argument)
{
    (void) argument;
}

static void kernel_context_test_valid(void)
{
    struct kernel_context context;
    int marker = 42;

    if (!kernel_context_init(
        &context,
        test_stack,
        sizeof(test_stack),
        kernel_context_test_entry,
        &marker
    )) {
        kernel_panic("Kernel context valid initialization failed");
    }

    if (
        context.stack_bottom != (uintptr_t) test_stack ||
        context.stack_top !=
            (uintptr_t) test_stack + sizeof(test_stack) ||
        context.entry != kernel_context_test_entry ||
        context.argument != &marker ||
        context.state != KERNEL_CONTEXT_READY
    ) {
        kernel_panic("Kernel context initialization contract failed");
    }
}

static void kernel_context_test_invalid_arguments(void)
{
    struct kernel_context context;

    if (
        kernel_context_init(
            NULL, test_stack, sizeof(test_stack),
            kernel_context_test_entry, NULL
        ) ||
        kernel_context_init(
            &context, NULL, sizeof(test_stack),
            kernel_context_test_entry, NULL
        ) ||
        kernel_context_init(
            &context, test_stack, sizeof(test_stack),
            NULL, NULL
        )
    ) {
        kernel_panic("Kernel context accepted invalid arguments");
    }
}

static void kernel_context_test_invalid_stack(void)
{
    struct kernel_context context;

    if (
        kernel_context_init(
            &context,
            test_stack + 1U,
            sizeof(test_stack) - 1U,
            kernel_context_test_entry,
            NULL
        ) ||
        kernel_context_init(
            &context,
            test_stack,
            KERNEL_CONTEXT_MIN_STACK_SIZE - 16U,
            kernel_context_test_entry,
            NULL
        ) ||
        kernel_context_init(
            &context,
            test_stack,
            KERNEL_CONTEXT_MIN_STACK_SIZE + 1U,
            kernel_context_test_entry,
            NULL
        )
    ) {
        kernel_panic("Kernel context accepted invalid stack");
    }
}

static void kernel_context_test_overflow(void)
{
    struct kernel_context context;

    /*
     * This value represents an invalid address range.
     * No memory is accessed through the synthetic pointer.
     */
    uintptr_t near_end =
        UINTPTR_MAX -
        (uintptr_t) (KERNEL_CONTEXT_STACK_ALIGNMENT - 1U);

    if (
        kernel_context_init(
            &context,
            (void *) near_end,
            KERNEL_CONTEXT_MIN_STACK_SIZE,
            kernel_context_test_entry,
            NULL
        )
    ) {
        kernel_panic("Kernel context accepted stack overflow");
    }
}

void kernel_context_test_run(void)
{
    kernel_context_test_valid();
    kernel_context_test_invalid_arguments();
    kernel_context_test_invalid_stack();
    kernel_context_test_overflow();

    diagnostics_write(
        "[kernel-context] Descriptor tests passed\n"
    );
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_stack_test.h"

#include <myos/kernel/arch/x86_64/interrupts.h>
#include <myos/kernel/arch/x86_64/kernel_stack.h>
#include <myos/kernel/core/panic.h>
#include "../diagnostics/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_STACK_TEST_SIZE 8192U
#define X86_RFLAGS_IF (1ULL << 9)

static _Alignas(16)
unsigned char test_stack[KERNEL_STACK_TEST_SIZE];

extern bool kernel_stack_abi_test(
    void *stack_top,
    kernel_stack_entry_fn entry,
    void *argument
);

struct kernel_stack_test_context {
    uintptr_t stack_bottom;
    uintptr_t stack_top;
    uintptr_t observed_rsp;
    bool executed;
};

static uintptr_t kernel_stack_test_read_rsp(void)
{
    uintptr_t rsp;

    __asm__ volatile (
        "movq %%rsp, %0"
        : "=r"(rsp)
        :
        : "memory"
    );

    return rsp;
}

static uint64_t kernel_stack_test_read_rflags(void)
{
    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        :
    );

    return rflags;
}

static void kernel_stack_test_callback(void *argument)
{
    struct kernel_stack_test_context *context = argument;

    if (context == NULL) {
        kernel_panic("Kernel stack callback received null argument");
    }

    uintptr_t rsp = kernel_stack_test_read_rsp();

    if (
        rsp < context->stack_bottom ||
        rsp >= context->stack_top
    ) {
        kernel_panic("Kernel stack callback used wrong stack");
    }

    if (
        (kernel_stack_test_read_rflags() & X86_RFLAGS_IF) != 0
    ) {
        kernel_panic("Kernel stack callback ran with interrupts enabled");
    }

    context->observed_rsp = rsp;
    context->executed = true;
}

static void kernel_stack_test_transition(void)
{
    struct kernel_stack_test_context context = {
        .stack_bottom = (uintptr_t) test_stack,
        .stack_top =
            (uintptr_t) test_stack + sizeof(test_stack),
    };

    /*
     * Guard bytes at the beginning of the alternative stack.
     * Normal downward stack growth must not reach this region.
     */
    for (size_t index = 0; index < 16U; ++index) {
        test_stack[index] = 0xA5;
    }

    uintptr_t original_rsp = kernel_stack_test_read_rsp();
    uint64_t original_rflags = kernel_stack_test_read_rflags();

    interrupts_disable();

    kernel_stack_call(
        (void *) context.stack_top,
        kernel_stack_test_callback,
        &context
    );

    uintptr_t restored_rsp = kernel_stack_test_read_rsp();

    /*
     * Restore the caller's previous maskable interrupt state.
     */
    if ((original_rflags & X86_RFLAGS_IF) != 0) {
        interrupts_enable();
    }

    if (
        !context.executed ||
        context.observed_rsp < context.stack_bottom ||
        context.observed_rsp >= context.stack_top
    ) {
        kernel_panic("Kernel stack callback execution failed");
    }

    if (restored_rsp != original_rsp) {
        kernel_panic("Kernel stack did not restore original RSP");
    }

    for (size_t index = 0; index < 16U; ++index) {
        if (test_stack[index] != 0xA5) {
            kernel_panic("Kernel stack overflowed guard bytes");
        }
    }
}

static void kernel_stack_test_abi(void)
{
    struct kernel_stack_test_context context = {
        .stack_bottom = (uintptr_t) test_stack,
        .stack_top =
            (uintptr_t) test_stack + sizeof(test_stack),
    };

    uint64_t original_rflags =
        kernel_stack_test_read_rflags();

    interrupts_disable();

    bool preserved = kernel_stack_abi_test(
        (void *) context.stack_top,
        kernel_stack_test_callback,
        &context
    );

    if ((original_rflags & X86_RFLAGS_IF) != 0) {
        interrupts_enable();
    }

    if (
        !preserved ||
        !context.executed
    ) {
        kernel_panic(
            "Kernel stack callee-saved register preservation failed"
        );
    }
}

void kernel_stack_test_run(void)
{
    kernel_stack_test_transition();
    kernel_stack_test_abi();

    diagnostics_write(
        "[kernel-context] Alternative stack tests passed\n"
    );
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_switch_test.h"

#include "../arch/x86_64/interrupts.h"
#include "../arch/x86_64/kernel_switch.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

extern bool kernel_switch_abi_test(
    struct kernel_switch_context *current,
    const struct kernel_switch_context *next
);

#define KERNEL_SWITCH_TEST_STACK_SIZE 8192U
#define X86_RFLAGS_IF (1ULL << 9)

static _Alignas(16)
unsigned char worker_stack[KERNEL_SWITCH_TEST_STACK_SIZE];

static struct kernel_switch_context main_context;
static struct kernel_switch_context worker_context;

static volatile unsigned int worker_stage;

static uint64_t kernel_switch_test_rflags(void)
{
    uint64_t flags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(flags)
        :
        : "memory"
    );

    return flags;
}

static void kernel_switch_test_worker(void)
{
    /*
     * This local variable must survive suspension.
     */
    volatile uint64_t marker = 0x1122334455667788ULL;

    worker_stage = 1U;

    kernel_context_switch(
        &worker_context,
        &main_context
    );

    /*
     * Execution must resume here, not at the
     * beginning of the worker function.
     */
    if (
        worker_stage != 1U ||
        marker != 0x1122334455667788ULL
    ) {
        kernel_panic(
            "Kernel switch lost worker continuation"
        );
    }

    marker = 0x8877665544332211ULL;
    worker_stage = 2U;

    kernel_context_switch(
        &worker_context,
        &main_context
    );

    /*
     * This test does not support a third resume.
     */
    kernel_panic(
        "Kernel switch resumed completed worker"
    );
}

static void kernel_switch_test_cooperative(void)
{
    main_context = (struct kernel_switch_context) {0};
    worker_context = (struct kernel_switch_context) {0};
    worker_stage = 0U;

    /*
     * Prepare a synthetic initial return address.
     *
     * The switch primitive resumes a context using RET.
     * After RET, RSP must satisfy the normal x86-64
     * function-entry alignment: RSP mod 16 == 8.
     *
     * Therefore, the initial saved RSP is top - 16.
     */
    uintptr_t stack_top =
        (uintptr_t) worker_stack +
        sizeof(worker_stack);

    typedef void (*kernel_test_entry_fn)(void);

    kernel_test_entry_fn *initial_return =
        (kernel_test_entry_fn *) (
            stack_top - 2U * sizeof(uintptr_t)
        );

    *initial_return = kernel_switch_test_worker;

    worker_context.rsp =
        (uint64_t) (uintptr_t) initial_return;

    uint64_t original_rflags =
        kernel_switch_test_rflags();

    interrupts_disable();

    /*
     * First entry into the worker.
     */
    kernel_context_switch(
        &main_context,
        &worker_context
    );

    if (worker_stage != 1U) {
        kernel_panic(
            "Kernel switch first suspension failed"
        );
    }

    /*
     * Resume the previously suspended worker.
     */
    kernel_context_switch(
        &main_context,
        &worker_context
    );

    if (worker_stage != 2U) {
        kernel_panic(
            "Kernel switch second suspension failed"
        );
    }

    /*
     * Restore the original interrupt state.
     */
    if ((original_rflags & X86_RFLAGS_IF) != 0) {
        interrupts_enable();
    }
}

static void kernel_switch_test_abi(void)
{
    main_context = (struct kernel_switch_context) {0};
    worker_context = (struct kernel_switch_context) {0};
    worker_stage = 0U;

    uintptr_t stack_top =
        (uintptr_t) worker_stack +
        sizeof(worker_stack);

    uintptr_t *initial_return =
        (uintptr_t *) (stack_top - 16U);

    *initial_return =
        (uintptr_t) kernel_switch_test_worker;

    worker_context.rsp =
        (uint64_t) (uintptr_t) initial_return;

    uint64_t original_rflags =
        kernel_switch_test_rflags();

    interrupts_disable();

    bool preserved = kernel_switch_abi_test(
        &main_context,
        &worker_context
    );

    if (
        !preserved ||
        worker_stage != 1U
    ) {
        kernel_panic(
            "Kernel switch callee-saved registers corrupted"
        );
    }

    /*
     * Resume the worker once more to verify that
     * its continuation remains usable.
     */
    kernel_context_switch(
        &main_context,
        &worker_context
    );

    if (worker_stage != 2U) {
        kernel_panic(
            "Kernel switch ABI worker continuation failed"
        );
    }

    if ((original_rflags & X86_RFLAGS_IF) != 0) {
        interrupts_enable();
    }
}

void kernel_switch_test_run(void)
{
    kernel_switch_test_cooperative();
    kernel_switch_test_abi();

    diagnostics_write(
        "[kernel-context] Cooperative switch tests passed\n"
    );
}

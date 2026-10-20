// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_switch_test.h"

#include "../arch/x86_64/interrupts.h"
#include <myos/kernel/arch/x86_64/irq_supervisor.h>
#include <myos/kernel/arch/x86_64/kernel_switch.h>
#include <myos/kernel/core/panic.h>
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

#define IRQ_SUPERVISOR_TEST_STACK_SIZE 8192U

static _Alignas(16)
unsigned char irq_test_entry_stack[
    IRQ_SUPERVISOR_TEST_STACK_SIZE
];

static _Alignas(16)
unsigned char irq_test_supervisor_stack[
    IRQ_SUPERVISOR_TEST_STACK_SIZE
];

static struct kernel_switch_context irq_test_caller;
static struct kernel_switch_context irq_test_entry;
static struct kernel_switch_context irq_test_supervisor;

static volatile unsigned int irq_test_stage;

static uint64_t irq_test_argument =
    0x1122334455667788ULL;

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

static void irq_test_supervisor_callback(
    void *argument)
{
    uintptr_t rsp;

    __asm__ volatile (
        "movq %%rsp, %0"
        : "=r"(rsp)
        :
        : "memory"
    );

    uint16_t cs;

    __asm__ volatile (
        "movw %%cs, %w0"
        : "=r"(cs)
    );

    uintptr_t bottom =
        (uintptr_t) irq_test_supervisor_stack;

    uintptr_t top =
        bottom + sizeof(irq_test_supervisor_stack);

    if (
        argument != &irq_test_argument ||
        irq_test_argument !=
            0x1122334455667788ULL ||
        irq_test_stage != 1U ||
        rsp < bottom ||
        rsp >= top ||
        (cs & 0x3U) != 0U ||
        (kernel_switch_test_rflags() &
            X86_RFLAGS_IF) != 0U
    ) {
        kernel_panic(
            "IRQ supervisor trampoline context invalid"
        );
    }

    irq_test_stage = 2U;

    /*
     * Test-only continuation:
     * return to the original kernel test runner.
     */
    kernel_context_switch(
        &irq_test_supervisor,
        &irq_test_caller
    );

    kernel_panic(
        "IRQ supervisor test callback unexpectedly resumed"
    );
}

static _Noreturn void irq_test_entry_point(void)
{
    if (irq_test_stage != 1U) {
        kernel_panic(
            "IRQ trampoline entered invalid test stage"
        );
    }

    irq_supervisor_enter(
        (uintptr_t) irq_test_supervisor_stack +
            sizeof(irq_test_supervisor_stack),
        irq_test_supervisor_callback,
        &irq_test_argument
    );
}

static void kernel_switch_test_irq_supervisor(void)
{
    irq_test_caller =
        (struct kernel_switch_context) {0};

    irq_test_entry =
        (struct kernel_switch_context) {0};

    irq_test_supervisor =
        (struct kernel_switch_context) {0};

    irq_test_stage = 1U;

    uintptr_t entry_bottom =
        (uintptr_t) irq_test_entry_stack;

    uintptr_t entry_top =
        entry_bottom + sizeof(irq_test_entry_stack);

    if (!kernel_switch_prepare(
        &irq_test_entry,
        entry_bottom,
        entry_top,
        irq_test_entry_point
    )) {
        kernel_panic(
            "Unable to prepare IRQ trampoline test entry"
        );
    }

    uint64_t original_rflags =
        kernel_switch_test_rflags();

    interrupts_disable();

    /*
     * Enter the synthetic kernel execution context.
     */
    kernel_context_switch(
        &irq_test_caller,
        &irq_test_entry
    );

    /*
     * This point is reached only after the supervisor
     * callback switches back to the original test runner.
     */
    if (irq_test_stage != 2U) {
        kernel_panic(
            "IRQ supervisor trampoline did not complete"
        );
    }

    if ((original_rflags & X86_RFLAGS_IF) != 0U) {
        interrupts_enable();
    }

    diagnostics_write(
        "[kernel-context] IRQ supervisor trampoline test passed\n"
    );
}

void kernel_switch_test_run(void)
{
    kernel_switch_test_cooperative();
    kernel_switch_test_abi();
    kernel_switch_test_irq_supervisor();

    diagnostics_write(
        "[kernel-context] Cooperative switch tests passed\n"
    );
}

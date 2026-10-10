// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_context_test.h"

#include "../arch/x86_64/interrupts.h"
#include "../arch/x86_64/kernel_switch.h"
#include "../core/execution/kernel_context.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
unsigned char test_stack[KERNEL_CONTEXT_MIN_STACK_SIZE];

#define KERNEL_CONTEXT_EXEC_STACK_SIZE 8192U
#define KERNEL_CONTEXT_RFLAGS_IF (1ULL << 9)

static _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
unsigned char execution_stack[KERNEL_CONTEXT_EXEC_STACK_SIZE];

static _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
unsigned char yield_stack[KERNEL_CONTEXT_EXEC_STACK_SIZE];

static _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
unsigned char isolation_stack_a[KERNEL_CONTEXT_EXEC_STACK_SIZE];

static _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
unsigned char isolation_stack_b[KERNEL_CONTEXT_EXEC_STACK_SIZE];

struct kernel_context_isolation_probe {
    struct kernel_context *self;
    struct kernel_context *other;
    unsigned int entry_calls;
    unsigned int stage;
    bool wrong_yield_rejected;
};

struct kernel_context_yield_probe {
    struct kernel_context *context;
    uintptr_t stack_bottom;
    uintptr_t stack_top;
    uintptr_t before_rsp;
    uintptr_t after_rsp;
    unsigned int entry_calls;
    unsigned int stage;
    bool resumed;
};

struct kernel_context_execution_probe {
    uintptr_t stack_bottom;
    uintptr_t stack_top;
    uintptr_t observed_rsp;
    unsigned int calls;
};

static uint64_t kernel_context_test_rflags(void)
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

static uintptr_t kernel_context_test_rsp(void)
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

static void kernel_context_test_entry(void *argument)
{
    (void) argument;
}

static void kernel_context_test_trampoline(void)
{
    kernel_panic("Unprepared trampoline was executed");
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

    if (
        context.cpu.rsp != 0 ||
        context.cpu.rbx != 0 ||
        context.cpu.rbp != 0 ||
        context.cpu.r12 != 0 ||
        context.cpu.r13 != 0 ||
        context.cpu.r14 != 0 ||
        context.cpu.r15 != 0
    ) {
        kernel_panic(
            "Kernel context CPU state was not initialized"
        );
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

static void kernel_context_test_prepare(void)
{
    struct kernel_context context;

    if (!kernel_context_init(
        &context,
        test_stack,
        sizeof(test_stack),
        kernel_context_test_entry,
        NULL
    )) {
        kernel_panic("Kernel context prepare setup failed");
    }

    if (
        context.prepared ||
        context.cpu.rsp != 0
    ) {
        kernel_panic("Kernel context unexpectedly prepared");
    }

    if (!kernel_context_prepare(
        &context,
        kernel_context_test_trampoline
    )) {
        kernel_panic("Kernel context preparation failed");
    }

    uintptr_t expected_rsp =
        context.stack_top - 16U;

    /*
     * The first kernel_context_switch() resumes using RET.
     * The saved RSP therefore points to the initial entry address.
     */
    uintptr_t *frame = (uintptr_t *) expected_rsp;

    if (
        !context.prepared ||
        context.state != KERNEL_CONTEXT_READY ||
        context.cpu.rsp != expected_rsp ||
        frame[0] !=
            (uintptr_t) kernel_context_test_trampoline ||
        ((context.cpu.rsp + sizeof(uintptr_t)) & 15U) != 8U
    ) {
        kernel_panic("Kernel context initial frame mismatch");
    }

    if (
        context.cpu.rbx != 0 ||
        context.cpu.rbp != 0 ||
        context.cpu.r12 != (uint64_t) (uintptr_t) &context ||
        context.cpu.r13 != 0 ||
        context.cpu.r14 != 0 ||
        context.cpu.r15 != 0
    ) {
        kernel_panic("Kernel context initial registers invalid");
    }
}

static void kernel_context_test_prepare_twice(void)
{
    struct kernel_context context;

    if (
        !kernel_context_init(
            &context,
            test_stack,
            sizeof(test_stack),
            kernel_context_test_entry,
            NULL
        ) ||
        !kernel_context_prepare(
            &context,
            kernel_context_test_trampoline
        )
    ) {
        kernel_panic("Kernel context double-prepare setup failed");
    }

    uintptr_t saved_rsp = context.cpu.rsp;
    uintptr_t saved_entry = *(uintptr_t *) saved_rsp;

    if (
        kernel_context_prepare(
            &context,
            kernel_context_test_trampoline
        ) ||
        context.cpu.rsp != saved_rsp ||
        *(uintptr_t *) saved_rsp != saved_entry ||
        !context.prepared
    ) {
        kernel_panic("Kernel context accepted second preparation");
    }
}

static void kernel_context_test_prepare_invalid(void)
{
    struct kernel_context context;

    if (!kernel_context_init(
        &context,
        test_stack,
        sizeof(test_stack),
        kernel_context_test_entry,
        NULL
    )) {
        kernel_panic("Kernel context invalid-prepare setup failed");
    }

    if (
        kernel_context_prepare(NULL, kernel_context_test_trampoline) ||
        kernel_context_prepare(&context, NULL) ||
        context.prepared ||
        context.cpu.rsp != 0
    ) {
        kernel_panic("Kernel context invalid preparation mutated state");
    }

    context.state = KERNEL_CONTEXT_RUNNING;

    if (
        kernel_context_prepare(
            &context,
            kernel_context_test_trampoline
        ) ||
        context.prepared ||
        context.cpu.rsp != 0
    ) {
        kernel_panic("Kernel context prepared a running descriptor");
    }

    context.state = KERNEL_CONTEXT_TERMINATED;

    if (
        kernel_context_prepare(
            &context,
            kernel_context_test_trampoline
        ) ||
        context.prepared ||
        context.cpu.rsp != 0
    ) {
        kernel_panic("Kernel context prepared a terminated descriptor");
    }
}

static void kernel_context_test_prepare_bounds(void)
{
    struct kernel_switch_context cpu = {
        .rsp = 0x1234U,
        .rbx = 0x5678U,
    };

    uintptr_t bottom = (uintptr_t) test_stack;
    uintptr_t top = bottom + sizeof(test_stack);

    if (
        kernel_switch_prepare(
            NULL, bottom, top,
            kernel_context_test_trampoline
        ) ||
        kernel_switch_prepare(
            &cpu, bottom, top, NULL
        ) ||
        kernel_switch_prepare(
            &cpu, bottom + 1U, top,
            kernel_context_test_trampoline
        ) ||
        kernel_switch_prepare(
            &cpu, bottom, top - 1U,
            kernel_context_test_trampoline
        ) ||
        kernel_switch_prepare(
            &cpu, bottom, bottom + 16U,
            kernel_context_test_trampoline
        )
    ) {
        kernel_panic("Kernel switch accepted invalid frame bounds");
    }

    if (
        cpu.rsp != 0x1234U ||
        cpu.rbx != 0x5678U
    ) {
        kernel_panic("Kernel switch invalid preparation mutated CPU state");
    }
}

static void kernel_context_test_supervisor(void)
{
    struct kernel_context context;
    struct kernel_switch_context supervisor = {0};

    if (!kernel_context_init(
        &context,
        test_stack,
        sizeof(test_stack),
        kernel_context_test_entry,
        NULL
    )) {
        kernel_panic("Kernel supervisor setup failed");
    }

    if (
        kernel_context_bind_supervisor(
            &context, &supervisor
        ) ||
        context.supervisor != NULL
    ) {
        kernel_panic("Unprepared kernel context accepted supervisor");
    }

    if (!kernel_context_prepare(
        &context,
        kernel_context_test_trampoline
    )) {
        kernel_panic("Kernel supervisor preparation failed");
    }

    if (
        kernel_context_bind_supervisor(&context, NULL) ||
        kernel_context_bind_supervisor(
            &context, &context.cpu
        ) ||
        !kernel_context_bind_supervisor(
            &context, &supervisor
        ) ||
        context.supervisor != &supervisor
    ) {
        kernel_panic("Kernel supervisor binding failed");
    }

    if (kernel_context_bind_supervisor(
        &context, &supervisor
    )) {
        kernel_panic("Kernel context accepted duplicate supervisor");
    }

    context.state = KERNEL_CONTEXT_TERMINATED;

    if (kernel_context_bind_supervisor(
        &context, &supervisor
    )) {
        kernel_panic("Terminated context accepted supervisor");
    }
}

static void kernel_context_test_execution_entry(void *argument)
{
    struct kernel_context_execution_probe *probe = argument;

    if (probe == NULL) {
        kernel_panic("Kernel context execution argument missing");
    }

    uintptr_t rsp = kernel_context_test_rsp();

    if (
        rsp < probe->stack_bottom ||
        rsp >= probe->stack_top
    ) {
        kernel_panic("Kernel context used incorrect execution stack");
    }

    if (
        (kernel_context_test_rflags() &
            KERNEL_CONTEXT_RFLAGS_IF) != 0
    ) {
        kernel_panic("Kernel context enabled interrupts");
    }

    probe->observed_rsp = rsp;
    ++probe->calls;
}

static void kernel_context_test_execution(void)
{
    struct kernel_context context;
    struct kernel_switch_context supervisor = {0};

    struct kernel_context_execution_probe probe = {
        .stack_bottom = (uintptr_t) execution_stack,
        .stack_top =
            (uintptr_t) execution_stack +
            sizeof(execution_stack),
    };

    for (size_t index = 0; index < 16U; ++index) {
        execution_stack[index] = 0xA5;
    }

    if (
        !kernel_context_init(
            &context,
            execution_stack,
            sizeof(execution_stack),
            kernel_context_test_execution_entry,
            &probe
        ) ||
        !kernel_context_prepare(
            &context,
            kernel_context_bootstrap
        ) ||
        !kernel_context_bind_supervisor(
            &context,
            &supervisor
        )
    ) {
        kernel_panic("Kernel context execution setup failed");
    }

    uintptr_t original_rsp = kernel_context_test_rsp();
    uint64_t original_rflags = kernel_context_test_rflags();

    interrupts_disable();

    /*
     * The test owns this transition until a lifecycle
     * API has been introduced.
     */
    if (!kernel_context_start(&context)) {
        kernel_panic("Unable to start prepared kernel context");
    }

    kernel_context_switch(
        &supervisor,
        &context.cpu
    );

    uintptr_t restored_rsp = kernel_context_test_rsp();

    if ((original_rflags & KERNEL_CONTEXT_RFLAGS_IF) != 0) {
        interrupts_enable();
    }

    if (
        context.state != KERNEL_CONTEXT_TERMINATED ||
        probe.calls != 1U ||
        probe.observed_rsp < probe.stack_bottom ||
        probe.observed_rsp >= probe.stack_top ||
        restored_rsp != original_rsp
    ) {
        kernel_panic("Kernel context execution lifecycle failed");
    }

    for (size_t index = 0; index < 16U; ++index) {
        if (execution_stack[index] != 0xA5) {
            kernel_panic("Kernel context execution stack overflow");
        }
    }
}

static void kernel_context_test_lifecycle(void)
{
    struct kernel_context context;
    struct kernel_switch_context supervisor = {0};

    if (!kernel_context_init(
        &context,
        test_stack,
        sizeof(test_stack),
        kernel_context_test_entry,
        NULL
    )) {
        kernel_panic("Kernel lifecycle setup failed");
    }

    /*
     * A descriptor is not executable until its stack
     * and supervisor have been prepared.
     */
    if (
        kernel_context_start(&context) ||
        context.state != KERNEL_CONTEXT_READY
    ) {
        kernel_panic("Unprepared kernel context started");
    }

    if (
        !kernel_context_prepare(
            &context,
            kernel_context_test_trampoline
        ) ||
        kernel_context_start(&context) ||
        !kernel_context_bind_supervisor(
            &context,
            &supervisor
        )
    ) {
        kernel_panic("Kernel lifecycle preparation failed");
    }

    if (
        !kernel_context_start(&context) ||
        context.state != KERNEL_CONTEXT_RUNNING ||
        !kernel_context_wait(&context) ||
        context.state != KERNEL_CONTEXT_WAITING ||
        !kernel_context_wake(&context) ||
        context.state != KERNEL_CONTEXT_READY ||
        !kernel_context_start(&context) ||
        context.state != KERNEL_CONTEXT_RUNNING ||
        !kernel_context_terminate(&context) ||
        context.state != KERNEL_CONTEXT_TERMINATED
    ) {
        kernel_panic("Kernel lifecycle transition failed");
    }

    if (
        kernel_context_start(&context) ||
        kernel_context_wait(&context) ||
        kernel_context_wake(&context) ||
        kernel_context_terminate(&context) ||
        context.state != KERNEL_CONTEXT_TERMINATED
    ) {
        kernel_panic("Terminated kernel context was reactivated");
    }
}

static void kernel_context_test_lifecycle_invalid(void)
{
    struct kernel_context context;
    struct kernel_switch_context supervisor = {0};

    if (
        !kernel_context_init(
            &context,
            test_stack,
            sizeof(test_stack),
            kernel_context_test_entry,
            NULL
        ) ||
        !kernel_context_prepare(
            &context,
            kernel_context_test_trampoline
        ) ||
        !kernel_context_bind_supervisor(
            &context,
            &supervisor
        )
    ) {
        kernel_panic("Kernel invalid lifecycle setup failed");
    }

    if (
        kernel_context_wait(NULL) ||
        kernel_context_wake(NULL) ||
        kernel_context_start(NULL) ||
        kernel_context_terminate(NULL)
    ) {
        kernel_panic("Kernel lifecycle accepted null context");
    }

    if (
        kernel_context_wait(&context) ||
        kernel_context_wake(&context) ||
        kernel_context_terminate(&context) ||
        context.state != KERNEL_CONTEXT_READY
    ) {
        kernel_panic("Kernel lifecycle accepted invalid READY transition");
    }

    if (!kernel_context_start(&context)) {
        kernel_panic("Kernel lifecycle could not enter RUNNING");
    }

    if (
        kernel_context_start(&context) ||
        kernel_context_wake(&context) ||
        context.state != KERNEL_CONTEXT_RUNNING
    ) {
        kernel_panic("Kernel lifecycle accepted invalid RUNNING transition");
    }

    if (!kernel_context_wait(&context)) {
        kernel_panic("Kernel lifecycle could not enter WAITING");
    }

    if (
        kernel_context_start(&context) ||
        kernel_context_wait(&context) ||
        kernel_context_terminate(&context) ||
        context.state != KERNEL_CONTEXT_WAITING
    ) {
        kernel_panic("Kernel lifecycle accepted invalid WAITING transition");
    }

    if (
        !kernel_context_wake(&context) ||
        !kernel_context_start(&context) ||
        !kernel_context_terminate(&context)
    ) {
        kernel_panic("Kernel lifecycle could not terminate");
    }

    if (
        kernel_context_terminate(&context) ||
        context.state != KERNEL_CONTEXT_TERMINATED
    ) {
        kernel_panic("Kernel lifecycle accepted double termination");
    }
}

static void kernel_context_test_yield_entry(void *argument)
{
    struct kernel_context_yield_probe *probe = argument;
    volatile uint64_t marker = 0x123456789ABCDEF0ULL;

    if (
        probe == NULL ||
        probe->context == NULL ||
        probe->stage != 0U
    ) {
        kernel_panic("Kernel yield entry contract failed");
    }

    ++probe->entry_calls;
    probe->before_rsp = kernel_context_test_rsp();

    if (
        probe->before_rsp < probe->stack_bottom ||
        probe->before_rsp >= probe->stack_top
    ) {
        kernel_panic("Kernel yield worker used wrong stack");
    }

    probe->stage = 1U;

    if (!kernel_context_yield_wait(probe->context)) {
        kernel_panic("Kernel yield failed");
    }

    /*
     * This code must execute only after the supervisor
     * has explicitly woken and restarted the worker.
     */
    probe->after_rsp = kernel_context_test_rsp();

    if (
        probe->stage != 2U ||
        marker != 0x123456789ABCDEF0ULL ||
        probe->entry_calls != 1U ||
        probe->after_rsp < probe->stack_bottom ||
        probe->after_rsp >= probe->stack_top
    ) {
        kernel_panic("Kernel yield continuation was not preserved");
    }

    probe->resumed = true;
    probe->stage = 3U;
}

static void kernel_context_test_yield_resume(void)
{
    struct kernel_context context;
    struct kernel_switch_context supervisor = {0};

    struct kernel_context_yield_probe probe = {
        .context = &context,
        .stack_bottom = (uintptr_t) yield_stack,
        .stack_top =
            (uintptr_t) yield_stack + sizeof(yield_stack),
    };

    if (
        !kernel_context_init(
            &context,
            yield_stack,
            sizeof(yield_stack),
            kernel_context_test_yield_entry,
            &probe
        ) ||
        !kernel_context_prepare(
            &context,
            kernel_context_bootstrap
        ) ||
        !kernel_context_bind_supervisor(
            &context,
            &supervisor
        )
    ) {
        kernel_panic("Kernel yield setup failed");
    }

    uint64_t original_rflags = kernel_context_test_rflags();

    interrupts_disable();

    /*
     * First dispatch: worker reaches yield_wait()
     * and returns control to this supervisor.
     */
    if (!kernel_context_start(&context)) {
        kernel_panic("Kernel yield initial start failed");
    }

    kernel_context_switch(&supervisor, &context.cpu);

    if (
        context.state != KERNEL_CONTEXT_WAITING ||
        probe.stage != 1U ||
        probe.entry_calls != 1U ||
        probe.resumed ||
        context.cpu.rsp < context.stack_bottom ||
        context.cpu.rsp >= context.stack_top
    ) {
        kernel_panic("Kernel yield did not suspend correctly");
    }

    /*
     * The supervisor controls wakeup and dispatch.
     * The worker must not resume before this point.
     */
    probe.stage = 2U;

    if (
        !kernel_context_wake(&context) ||
        context.state != KERNEL_CONTEXT_READY ||
        !kernel_context_start(&context)
    ) {
        kernel_panic("Kernel yield restart failed");
    }

    kernel_context_switch(&supervisor, &context.cpu);

    if ((original_rflags & KERNEL_CONTEXT_RFLAGS_IF) != 0) {
        interrupts_enable();
    }

    if (
        context.state != KERNEL_CONTEXT_TERMINATED ||
        probe.stage != 3U ||
        !probe.resumed ||
        probe.entry_calls != 1U
    ) {
        kernel_panic("Kernel yield resume lifecycle failed");
    }
}

static void kernel_context_test_stack_ownership(void)
{
    struct kernel_context context;

    if (!kernel_context_init(
        &context,
        test_stack,
        sizeof(test_stack),
        kernel_context_test_entry,
        NULL
    )) {
        kernel_panic("Kernel stack ownership setup failed");
    }

    uintptr_t bottom = context.stack_bottom;
    uintptr_t top = context.stack_top;

    if (
        !kernel_context_contains_stack_pointer(
            &context, bottom
        ) ||
        !kernel_context_contains_stack_pointer(
            &context, top - 1U
        ) ||
        kernel_context_contains_stack_pointer(
            &context, bottom - 1U
        ) ||
        kernel_context_contains_stack_pointer(
            &context, top
        ) ||
        kernel_context_contains_stack_pointer(
            NULL, bottom
        )
    ) {
        kernel_panic("Kernel stack ownership contract failed");
    }

    struct kernel_context invalid = {0};

    if (kernel_context_contains_stack_pointer(
        &invalid, bottom
    )) {
        kernel_panic("Invalid context accepted stack pointer");
    }
}

static void kernel_context_test_yield_wrong_stack(void)
{
    struct kernel_context context;
    struct kernel_switch_context supervisor = {0};

    if (
        !kernel_context_init(
            &context,
            yield_stack,
            sizeof(yield_stack),
            kernel_context_test_entry,
            NULL
        ) ||
        !kernel_context_prepare(
            &context,
            kernel_context_bootstrap
        ) ||
        !kernel_context_bind_supervisor(
            &context,
            &supervisor
        ) ||
        !kernel_context_start(&context)
    ) {
        kernel_panic("Kernel wrong-stack yield setup failed");
    }

    uintptr_t saved_rsp = context.cpu.rsp;

    /*
     * We are executing on the test supervisor's stack,
     * not on yield_stack.
     *
     * The worker is logically RUNNING, but this caller
     * must not be allowed to suspend its descriptor.
     */
    if (
        kernel_context_yield_wait(&context) ||
        context.state != KERNEL_CONTEXT_RUNNING ||
        context.cpu.rsp != saved_rsp
    ) {
        kernel_panic("Kernel context accepted wrong-stack yield");
    }

    /*
     * Restore the descriptor's logical state.
     * No actual execution occurred.
     */
    if (!kernel_context_terminate(&context)) {
        kernel_panic("Kernel wrong-stack test cleanup failed");
    }
}

static void kernel_context_test_isolation_entry_a(void *argument)
{
    struct kernel_context_isolation_probe *probe = argument;

    if (
        probe == NULL ||
        probe->self == NULL ||
        probe->other == NULL
    ) {
        kernel_panic("Kernel isolation A argument invalid");
    }

    ++probe->entry_calls;

    if (probe->stage != 0U) {
        kernel_panic("Kernel isolation A restarted unexpectedly");
    }

    uintptr_t other_rsp = probe->other->cpu.rsp;
    enum kernel_context_state other_state = probe->other->state;

    /*
     * Worker A is executing on isolation_stack_a.
     * It must not suspend worker B's descriptor.
     */
    if (
        kernel_context_yield_wait(probe->other) ||
        probe->other->state != other_state ||
        probe->other->cpu.rsp != other_rsp
    ) {
        kernel_panic("Kernel isolation accepted foreign context yield");
    }

    probe->wrong_yield_rejected = true;
    probe->stage = 1U;

    if (!kernel_context_yield_wait(probe->self)) {
        kernel_panic("Kernel isolation A suspension failed");
    }

    if (probe->stage != 2U || probe->entry_calls != 1U) {
        kernel_panic("Kernel isolation A continuation invalid");
    }

    probe->stage = 3U;
}

static void kernel_context_test_isolation_entry_b(void *argument)
{
    struct kernel_context_isolation_probe *probe = argument;

    if (
        probe == NULL ||
        probe->stage != 0U
    ) {
        kernel_panic("Kernel isolation B argument invalid");
    }

    ++probe->entry_calls;
    probe->stage = 1U;
}

static void kernel_context_test_worker_isolation(void)
{
    struct kernel_context context_a;
    struct kernel_context context_b;
    struct kernel_switch_context supervisor = {0};

    struct kernel_context_isolation_probe probe_a = {
        .self = &context_a,
        .other = &context_b,
    };

    struct kernel_context_isolation_probe probe_b = {
        .self = &context_b,
        .other = &context_a,
    };

    if (
        !kernel_context_init(
            &context_a,
            isolation_stack_a,
            sizeof(isolation_stack_a),
            kernel_context_test_isolation_entry_a,
            &probe_a
        ) ||
        !kernel_context_init(
            &context_b,
            isolation_stack_b,
            sizeof(isolation_stack_b),
            kernel_context_test_isolation_entry_b,
            &probe_b
        ) ||
        !kernel_context_prepare(
            &context_a,
            kernel_context_bootstrap
        ) ||
        !kernel_context_prepare(
            &context_b,
            kernel_context_bootstrap
        ) ||
        !kernel_context_bind_supervisor(
            &context_a, &supervisor
        ) ||
        !kernel_context_bind_supervisor(
            &context_b, &supervisor
        )
    ) {
        kernel_panic("Kernel worker isolation setup failed");
    }

    uint64_t original_rflags = kernel_context_test_rflags();

    interrupts_disable();

    /*
     * Both descriptors are logically RUNNING for this
     * negative test. Only A executes initially.
     *
     * The deliberate state arrangement tests stack
     * ownership independently of the scheduling policy.
     */
    if (
        !kernel_context_start(&context_b) ||
        !kernel_context_start(&context_a)
    ) {
        kernel_panic("Kernel worker isolation start failed");
    }

    /*
     * A rejects a request to suspend B, then suspends
     * itself and returns to the supervisor.
     */
    kernel_context_switch(&supervisor, &context_a.cpu);

    if (
        context_a.state != KERNEL_CONTEXT_WAITING ||
        context_b.state != KERNEL_CONTEXT_RUNNING ||
        !probe_a.wrong_yield_rejected ||
        probe_a.stage != 1U ||
        probe_a.entry_calls != 1U ||
        probe_b.entry_calls != 0U
    ) {
        kernel_panic("Kernel worker isolation first dispatch failed");
    }

    /*
     * B's initial frame must remain executable.
     */
    kernel_context_switch(&supervisor, &context_b.cpu);

    if (
        context_b.state != KERNEL_CONTEXT_TERMINATED ||
        probe_b.entry_calls != 1U ||
        probe_b.stage != 1U
    ) {
        kernel_panic("Kernel worker isolation B completion failed");
    }

    probe_a.stage = 2U;

    if (
        !kernel_context_wake(&context_a) ||
        !kernel_context_start(&context_a)
    ) {
        kernel_panic("Kernel worker isolation A wake failed");
    }

    /*
     * A resumes immediately after its own yield.
     */
    kernel_context_switch(&supervisor, &context_a.cpu);

    if ((original_rflags & KERNEL_CONTEXT_RFLAGS_IF) != 0) {
        interrupts_enable();
    }

    if (
        context_a.state != KERNEL_CONTEXT_TERMINATED ||
        context_b.state != KERNEL_CONTEXT_TERMINATED ||
        probe_a.stage != 3U ||
        probe_a.entry_calls != 1U ||
        probe_b.entry_calls != 1U
    ) {
        kernel_panic("Kernel worker isolation lifecycle failed");
    }
}

void kernel_context_test_run(void)
{
    kernel_context_test_valid();
    kernel_context_test_invalid_arguments();
    kernel_context_test_invalid_stack();
    kernel_context_test_overflow();

    kernel_context_test_prepare();
    kernel_context_test_prepare_twice();
    kernel_context_test_prepare_invalid();
    kernel_context_test_prepare_bounds();

    kernel_context_test_supervisor();

    kernel_context_test_execution();

    kernel_context_test_yield_resume();
    kernel_context_test_yield_wrong_stack();
    kernel_context_test_worker_isolation();

    kernel_context_test_stack_ownership();

    kernel_context_test_lifecycle();
    kernel_context_test_lifecycle_invalid();

    diagnostics_write(
        "[kernel-context] Descriptor tests passed\n"
    );
}

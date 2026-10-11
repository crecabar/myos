// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file scheduler_slot_test.c
 * @brief Scheduler registration-slot recycling regression tests.
 */

#include "scheduler_slot_test.h"

#include "../arch/x86_64/interrupts.h"
#include "../arch/x86_64/timer.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"

#include <stddef.h>
#include <stdint.h>

#define SCHEDULER_SLOT_TEST_PROCESS_COUNT \
    (SCHEDULER_MAX_PROCESSES + 1)

#define SCHEDULER_SLOT_TEST_RECYCLED_INDEX 3

#define SCHEDULER_SLOT_TEST_ENTRY_BASE \
    0x0000000000400000ULL

#define SCHEDULER_SLOT_TEST_STACK_TOP \
    0x0000000070000000ULL

#define SCHEDULER_SLOT_TEST_OLD_RAX \
    0x1111222233334444ULL

#define SCHEDULER_SLOT_TEST_OLD_RBX \
    0x5555666677778888ULL

#define SCHEDULER_SLOT_TEST_NEW_RAX \
    0xAAAABBBBCCCCDDDDULL

#define SCHEDULER_SLOT_TEST_NEW_RBX \
    0xEEEEFFFF11112222ULL

static struct process_memory
    test_memories[SCHEDULER_SLOT_TEST_PROCESS_COUNT];

static struct process_layout
    test_layouts[SCHEDULER_SLOT_TEST_PROCESS_COUNT];

static struct process
    test_processes[SCHEDULER_SLOT_TEST_PROCESS_COUNT];

static unsigned int idle_work_test_calls;

static bool scheduler_slot_test_idle_work(void)
{
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
            "Scheduler idle work ran with interrupts enabled"
        );
    }

    ++idle_work_test_calls;

    return idle_work_test_calls == 1U;
}

static void scheduler_slot_test_initialize_processes(void);

void scheduler_slot_test_run(void)
{
    if (scheduler_current() != NULL) {
        kernel_panic(
            "Scheduler slot test started with active process"
        );
    }

    /*
     * No real processes have been registered yet when this regression runs.
     */
    scheduler_init();

        /*
     * Exercise the production idle-work dispatch helper
     * without entering the non-returning idle loop.
     */
    idle_work_test_calls = 0;

    interrupts_disable();

    if (scheduler_test_dispatch_idle_work()) {
        kernel_panic(
            "Scheduler idle work ran without a handler"
        );
    }

    scheduler_set_idle_work_handler(
        scheduler_slot_test_idle_work
    );

    if (
        !scheduler_test_dispatch_idle_work() ||
        idle_work_test_calls != 1U
    ) {
        kernel_panic(
            "Scheduler idle work dispatch failed"
        );
    }

    if (
        scheduler_test_dispatch_idle_work() ||
        idle_work_test_calls != 2U
    ) {
        kernel_panic(
            "Scheduler idle work completion contract failed"
        );
    }

    scheduler_set_idle_work_handler(NULL);

    if (
        scheduler_test_dispatch_idle_work() ||
        idle_work_test_calls != 2U
    ) {
        kernel_panic(
            "Scheduler idle work unregister failed"
        );
    }

    interrupts_enable();

    /*
     * Kernel worker registration owns no execution resources
     * and must not interfere with userspace process slots.
     */
    struct kernel_context worker = {
        .state = KERNEL_CONTEXT_READY,
        .prepared = true,
        .supervisor = NULL,
        .owner_cpu_id = KERNEL_CONTEXT_NO_CPU,
    };

    struct kernel_switch_context supervisor = {0};

    if (scheduler_register_kernel_worker(&worker)) {
        kernel_panic(
            "Scheduler accepted worker without supervisor"
        );
    }

    worker.supervisor = &supervisor;

    if (
        !scheduler_register_kernel_worker(&worker) ||
        scheduler_register_kernel_worker(&worker)
    ) {
        kernel_panic(
            "Scheduler kernel worker registration failed"
        );
    }

    struct kernel_context other_worker = {
        .state = KERNEL_CONTEXT_READY,
        .prepared = true,
        .supervisor = &supervisor,
        .owner_cpu_id = KERNEL_CONTEXT_NO_CPU,
    };

    if (
        scheduler_register_kernel_worker(&other_worker) ||
        scheduler_unregister_kernel_worker(&other_worker)
    ) {
        kernel_panic(
            "Scheduler accepted unrelated kernel worker"
        );
    }

    worker.state = KERNEL_CONTEXT_RUNNING;

    if (scheduler_unregister_kernel_worker(&worker)) {
        kernel_panic(
            "Scheduler unregistered running kernel worker"
        );
    }

    worker.state = KERNEL_CONTEXT_WAITING;

    if (
        !scheduler_unregister_kernel_worker(&worker) ||
        scheduler_unregister_kernel_worker(&worker)
    ) {
        kernel_panic(
            "Scheduler kernel worker unregister failed"
        );
    }

    scheduler_slot_test_initialize_processes();

    /*
     * Only READY processes may enter the scheduler.
     * Test this while registration capacity is still available,
     * so a rejection cannot be attributed to a full slot table.
     */
    test_processes[0].state = PROCESS_STATE_BLOCKED;

    if (scheduler_add(&test_processes[0])) {
        kernel_panic(
            "Scheduler registered a BLOCKED process"
        );
    }

    test_processes[0].state = PROCESS_STATE_SLEEPING;

    if (scheduler_add(&test_processes[0])) {
        kernel_panic(
            "Scheduler registered a SLEEPING process"
        );
    }

    test_processes[0].state = PROCESS_STATE_READY;

    if (!scheduler_add(&test_processes[0])) {
        kernel_panic(
            "Unable to register first scheduler slot test process"
        );
    }

    if (scheduler_add(&test_processes[0])) {
        kernel_panic(
            "Scheduler accepted duplicate process registration"
        );
    }

    for (size_t index = 1;
        index < SCHEDULER_MAX_PROCESSES;
        ++index) {
        if (!scheduler_add(&test_processes[index])) {
            kernel_panic(
                "Unable to fill scheduler slot table"
            );
        }
    }

    /*
     * Exercise wakeup accounting on registered test processes.
     *
     * Interrupts remain disabled while synthetic timer values
     * are supplied so real timer interrupts cannot interfere
     * with the test metadata.
     *
     * The direct state assignments below are test-only setup.
     * Production transitions will be owned by the scheduler.
     */
    struct process *sleeper = &test_processes[0];
    struct process *blocked = &test_processes[1];

    interrupts_disable();

    sleeper->sleep_start_ticks = UINT64_MAX - 1U;
    sleeper->sleep_duration_ticks = 3;
    sleeper->state = PROCESS_STATE_SLEEPING;

    blocked->state = PROCESS_STATE_BLOCKED;

    /*
     * The interval begins two ticks before the uint64_t
     * counter wraps. Tick zero must not wake this process.
     */
    scheduler_wake_sleepers(0);

    if (
        sleeper->state != PROCESS_STATE_SLEEPING ||
        blocked->state != PROCESS_STATE_BLOCKED
    ) {
        kernel_panic(
            "Scheduler woke a process before timer expiration"
        );
    }

    /*
     * At tick one, three ticks have elapsed across wraparound.
     */
    scheduler_wake_sleepers(1);

    if (
        sleeper->state != PROCESS_STATE_READY ||
        sleeper->sleep_start_ticks != 0 ||
        sleeper->sleep_duration_ticks != 0
    ) {
        kernel_panic(
            "Scheduler failed to wake expired sleeping process"
        );
    }

    if (blocked->state != PROCESS_STATE_BLOCKED) {
        kernel_panic(
            "Timer incorrectly woke a BLOCKED process"
        );
    }

    /*
     * Verify integration with actual timer interrupts.
     *
     * This test executes in kernel mode, so periodic timer
     * interrupts may update process states without invoking
     * user-mode scheduling.
     */
    uint64_t live_start = timer_ticks();

    sleeper->sleep_start_ticks = live_start;
    sleeper->sleep_duration_ticks = 2;
    sleeper->state = PROCESS_STATE_SLEEPING;

    while (
        sleeper->state == PROCESS_STATE_SLEEPING &&
        (uint64_t) (timer_ticks() - live_start) < 8
    ) {
        interrupts_wait();
    }

    if (
        sleeper->state != PROCESS_STATE_READY ||
        sleeper->sleep_start_ticks != 0 ||
        sleeper->sleep_duration_ticks != 0
    ) {
        kernel_panic(
            "Timer interrupt failed to wake sleeping process"
        );
    }

    if (
        (uint64_t) (timer_ticks() - live_start) < 2
    ) {
        kernel_panic(
            "Timer woke sleeping process before requested interval"
        );
    }

    if (blocked->state != PROCESS_STATE_BLOCKED) {
        kernel_panic(
            "Timer interrupt incorrectly woke BLOCKED process"
        );
    }

    /*
     * Reject an unregistered descriptor, even when its
     * state field says BLOCKED.
     */
    struct process *unregistered =
        &test_processes[SCHEDULER_MAX_PROCESSES];

    unregistered->state = PROCESS_STATE_BLOCKED;

    if (
        scheduler_wake_blocked(unregistered) ||
        unregistered->state != PROCESS_STATE_BLOCKED
    ) {
        kernel_panic(
            "Scheduler woke an unregistered BLOCKED process"
        );
    }

    unregistered->state = PROCESS_STATE_READY;

    /*
     * A READY process cannot be woken again.
     */
    if (
        scheduler_wake_blocked(sleeper) ||
        sleeper->state != PROCESS_STATE_READY
    ) {
        kernel_panic(
            "Scheduler accepted wakeup of a READY process"
        );
    }

    /*
     * Explicitly wake the registered BLOCKED process.
     */
    if (
        !scheduler_wake_blocked(blocked) ||
        blocked->state != PROCESS_STATE_READY
    ) {
        kernel_panic(
            "Scheduler failed to wake a BLOCKED process"
        );
    }

    /*
     * The transition must be one-shot.
     */
    if (scheduler_wake_blocked(blocked)) {
        kernel_panic(
            "Scheduler accepted duplicate BLOCKED wakeup"
        );
    }

    interrupts_enable();

    diagnostics_write(
        "[scheduler] Timer-driven wakeup tests passed\n"
    );

    diagnostics_write(
        "[scheduler] Explicit BLOCKED wakeup tests passed\n"
    );

    struct process *replacement =
        &test_processes[SCHEDULER_MAX_PROCESSES];

    if (scheduler_add(replacement)) {
        kernel_panic(
            "Scheduler accepted process beyond slot capacity"
        );
    }

    struct process *recycled =
        &test_processes[
            SCHEDULER_SLOT_TEST_RECYCLED_INDEX
        ];

    /*
     * READY registrations cannot be removed through the terminated-process
     * lifecycle path.
     */
    if (scheduler_unregister_terminated(recycled)) {
        kernel_panic(
            "Scheduler unregistered non-terminated process"
        );
    }

    /*
     * The rejected unregister must not have released capacity.
     */
    if (scheduler_add(replacement)) {
        kernel_panic(
            "Rejected scheduler unregister released a slot"
        );
    }

    recycled->state =
        PROCESS_STATE_TERMINATED;

    recycled->termination_reason =
        PROCESS_TERMINATION_EXITED;

    recycled->exit_status = 77;

    recycled->context.rax =
        SCHEDULER_SLOT_TEST_OLD_RAX;

    recycled->context.rbx =
        SCHEDULER_SLOT_TEST_OLD_RBX;

    uint64_t recycled_id =
        recycled->id;

    if (!scheduler_unregister_terminated(recycled)) {
        kernel_panic(
            "Unable to release terminated scheduler slot"
        );
    }

    if (
        recycled->id != recycled_id ||
        recycled->state != PROCESS_STATE_TERMINATED ||
        recycled->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        recycled->exit_status != 77 ||
        recycled->context.rax !=
            SCHEDULER_SLOT_TEST_OLD_RAX ||
        recycled->context.rbx !=
            SCHEDULER_SLOT_TEST_OLD_RBX
    ) {
        kernel_panic(
            "Scheduler unregister mutated process metadata"
        );
    }

    if (scheduler_unregister_terminated(recycled)) {
        kernel_panic(
            "Scheduler unregistered process more than once"
        );
    }

    replacement->context.rax =
        SCHEDULER_SLOT_TEST_NEW_RAX;

    replacement->context.rbx =
        SCHEDULER_SLOT_TEST_NEW_RBX;

    uint64_t replacement_id =
        replacement->id;

    if (!scheduler_add(replacement)) {
        kernel_panic(
            "Scheduler did not reuse released slot capacity"
        );
    }

    if (
        replacement->id != replacement_id ||
        replacement->state != PROCESS_STATE_READY ||
        replacement->termination_reason !=
            PROCESS_TERMINATION_NONE ||
        replacement->exit_status != 0 ||
        replacement->context.rax !=
            SCHEDULER_SLOT_TEST_NEW_RAX ||
        replacement->context.rbx !=
            SCHEDULER_SLOT_TEST_NEW_RBX
    ) {
        kernel_panic(
            "Scheduler slot reuse leaked stale process state"
        );
    }

    if (scheduler_add(replacement)) {
        kernel_panic(
            "Scheduler accepted duplicate replacement process"
        );
    }

    if (scheduler_current() != NULL) {
        kernel_panic(
            "Scheduler registration unexpectedly selected a process"
        );
    }

    /*
     * Remove every test registration so the real user-process fixtures begin
     * with a completely empty scheduler.
     */
    for (size_t index = 0;
         index < SCHEDULER_MAX_PROCESSES;
         ++index) {
        if (
            index ==
            SCHEDULER_SLOT_TEST_RECYCLED_INDEX
        ) {
            continue;
        }

        test_processes[index].state =
            PROCESS_STATE_TERMINATED;

        if (!scheduler_unregister_terminated(
            &test_processes[index]
        )) {
            kernel_panic(
                "Unable to clean scheduler slot test registration"
            );
        }
    }

    replacement->state =
        PROCESS_STATE_TERMINATED;

    if (!scheduler_unregister_terminated(replacement)) {
        kernel_panic(
            "Unable to clean reused scheduler slot"
        );
    }

    diagnostics_write(
        "[scheduler] Scheduler slot recycling test passed\n"
    );
}

static void scheduler_slot_test_initialize_processes(void)
{
    for (size_t index = 0;
         index < SCHEDULER_SLOT_TEST_PROCESS_COUNT;
         ++index) {
        test_layouts[index].entry_point =
            SCHEDULER_SLOT_TEST_ENTRY_BASE +
            ((uint64_t) index * 0x1000ULL);

            test_layouts[index].stack.stack_top =
            SCHEDULER_SLOT_TEST_STACK_TOP -
            ((uint64_t) index * 0x1000ULL);

        /*
         * Simulate stale metadata in reused process storage.
         * process_init() must reset both timer-wait fields.
         */
        test_processes[index].sleep_start_ticks = UINT64_MAX;
        test_processes[index].sleep_duration_ticks = UINT64_MAX;

        if (!process_init(
            &test_processes[index],
            100ULL + (uint64_t) index,
            &test_memories[index],
            &test_layouts[index]
        )) {
            kernel_panic(
                "Unable to initialize scheduler slot test process"
            );
        }

        if (
            test_processes[index].state != PROCESS_STATE_READY ||
            test_processes[index].sleep_start_ticks != 0 ||
            test_processes[index].sleep_duration_ticks != 0
        ) {
            kernel_panic(
                "Process initialization retained stale sleep metadata"
            );
        }
    }
}

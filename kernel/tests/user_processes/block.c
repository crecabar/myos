// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file block.c
 * @brief Ring-3 explicit BLOCKED/wakeup regression fixture.
 */

#include "block.h"
#include "fixture.h"

#include "../../arch/x86_64/tests/user_programs.h"
#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../memory/memory.h"
#include "../../process/process.h"
#include "../../scheduler/scheduler.h"

#include <stdbool.h>
#include <stdint.h>

// Defines
#define USER_PROCESS_BLOCK_TEST_PID 115
#define USER_PROCESS_EVENT_WORKER_TEST_PID 116
#define USER_PROCESS_BLOCK_TEST_EXIT_STATUS 43

// Static local variables
static struct user_process_fixture block_test_fixture;
static struct user_process_fixture event_worker_test_fixture;

static bool event_worker_completed;

static uint64_t block_test_free_frame_baseline;

// Public functions implementations
void user_process_block_test_prepare(void)
{
    block_test_free_frame_baseline =
        physical_free_frame_count();

    event_worker_completed = false;

    /*
     * Register the blocking process first. Its syscall must transfer
     * execution to the event worker, which remains READY.
     */
    user_process_fixture_prepare(
        &block_test_fixture,
        USER_PROCESS_BLOCK_TEST_PID,
        user_program_block_probe()
    );

    user_process_fixture_prepare(
        &event_worker_test_fixture,
        USER_PROCESS_EVENT_WORKER_TEST_PID,
        user_program_survivor()
    );
}

bool user_process_block_test_terminated(
    struct process *process)
{
    if (process == &event_worker_test_fixture.process) {
        if (event_worker_completed) {
            kernel_panic(
                "Event worker completed more than once"
            );
        }

        /*
         * The worker's termination is the event that releases the blocked
         * process. Verify its state before waking it.
         */
        if (
            block_test_fixture.process.state !=
                PROCESS_STATE_BLOCKED
        ) {
            kernel_panic(
                "Event worker found process outside BLOCKED state"
            );
        }

        if (
            process->id != USER_PROCESS_EVENT_WORKER_TEST_PID ||
            process->state != PROCESS_STATE_TERMINATED ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status != 0
        ) {
            kernel_panic(
                "Event worker produced unexpected termination result"
            );
        }

        if (!process_reclaim_resources(process)) {
            kernel_panic(
                "Unable to reclaim event worker resources"
            );
        }

        /*
         * The scheduler has detached the terminated worker and activated
         * kernel address space before invoking this handler. The interrupt
         * path still has IF disabled.
         */
        if (!scheduler_wake_blocked(
            &block_test_fixture.process
        )) {
            kernel_panic(
                "Unable to wake process after event worker termination"
            );
        }

        if (
            block_test_fixture.process.state !=
                PROCESS_STATE_READY ||
            scheduler_wake_blocked(
                &block_test_fixture.process
            )
        ) {
            kernel_panic(
                "Explicit event wakeup violated state transition"
            );
        }

        event_worker_completed = true;

        diagnostics_write(
            "[scheduler] Event worker woke BLOCKED process\n"
        );

        return false;
    }

    if (process != &block_test_fixture.process) {
        kernel_panic(
            "BLOCKED test received unexpected terminated process"
        );
    }

    if (!event_worker_completed) {
        kernel_panic(
            "BLOCKED process resumed before event completion"
        );
    }

    if (
        process->id != USER_PROCESS_BLOCK_TEST_PID ||
        process->state != PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status !=
            USER_PROCESS_BLOCK_TEST_EXIT_STATUS ||
        process->sleep_start_ticks != 0 ||
        process->sleep_duration_ticks != 0
    ) {
        kernel_panic(
            "BLOCKED process did not resume with preserved context"
        );
    }

    if (!process_reclaim_resources(process)) {
        kernel_panic(
            "Unable to reclaim BLOCKED test process resources"
        );
    }

    if (
        physical_free_frame_count() !=
        block_test_free_frame_baseline
    ) {
        kernel_panic(
            "BLOCKED test leaked physical frames"
        );
    }

    diagnostics_write(
        "[scheduler] User BLOCKED/event wakeup test passed\n"
    );

    return true;
}

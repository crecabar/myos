// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file sleep.c
 * @brief Ring-3 scheduler sleep/resume regression fixtures.
 */

#include "sleep.h"
#include "fixture.h"

#include "../../arch/x86_64/tests/user_programs.h"
#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../memory/memory.h"
#include "../../process/process.h"

#include <stdbool.h>
#include <stdint.h>

// Defines
#define USER_PROCESS_SLEEP_TEST_PID 112
#define USER_PROCESS_SLEEP_TEST_EXIT_STATUS 42

#define USER_PROCESS_MULTI_SLEEP_TEST_PID 113
#define USER_PROCESS_MULTI_WORKER_TEST_PID 114

// Static local variables
static struct user_process_fixture sleep_test_fixture;

static struct user_process_fixture multi_sleep_test_fixture;
static struct user_process_fixture multi_worker_test_fixture;

static bool multi_worker_completed;

static uint64_t sleep_test_free_frame_baseline;

// Private functions and helpers declarations
static void user_process_sleep_test_prepare_multi(void);

// Public functions implementations
void user_process_sleep_test_prepare(void)
{
    sleep_test_free_frame_baseline =
        physical_free_frame_count();

    multi_worker_completed = false;

    user_process_fixture_prepare(
        &sleep_test_fixture,
        USER_PROCESS_SLEEP_TEST_PID,
        user_program_sleep_probe()
    );
}

bool user_process_sleep_test_terminated(
    struct process *process)
{
    if (process == &sleep_test_fixture.process) {
        if (
            process->id != USER_PROCESS_SLEEP_TEST_PID ||
            process->state != PROCESS_STATE_TERMINATED ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status !=
                USER_PROCESS_SLEEP_TEST_EXIT_STATUS
        ) {
            kernel_panic(
                "Sleep test did not resume with the expected result"
            );
        }

        if (
            process->sleep_start_ticks != 0 ||
            process->sleep_duration_ticks != 0
        ) {
            kernel_panic(
                "Sleep test retained timer metadata after wakeup"
            );
        }

        if (!process_reclaim_resources(process)) {
            kernel_panic(
                "Unable to reclaim sleep test process resources"
            );
        }

        if (
            physical_free_frame_count() !=
            sleep_test_free_frame_baseline
        ) {
            kernel_panic(
                "Sleep test leaked physical frames"
            );
        }

        diagnostics_write(
            "[scheduler] User sleep/resume via idle test passed\n"
        );

        user_process_sleep_test_prepare_multi();

        return false;
    }

    /*
     * The worker must finish while the sleeper is still SLEEPING. This proves
     * that the sleeping process did not consume a scheduling turn while the
     * worker ran.
     */
    if (process == &multi_worker_test_fixture.process) {
        if (multi_worker_completed) {
            kernel_panic(
                "Multi-process sleep worker completed twice"
            );
        }

        if (
            multi_sleep_test_fixture.process.state !=
                PROCESS_STATE_SLEEPING
        ) {
            kernel_panic(
                "Worker did not execute while sleeper was sleeping"
            );
        }

        if (
            process->id != USER_PROCESS_MULTI_WORKER_TEST_PID ||
            process->state != PROCESS_STATE_TERMINATED ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status != 0
        ) {
            kernel_panic(
                "Multi-process sleep worker produced unexpected result"
            );
        }

        if (!process_reclaim_resources(process)) {
            kernel_panic(
                "Unable to reclaim multi-process sleep worker"
            );
        }

        multi_worker_completed = true;

        diagnostics_write(
            "[scheduler] Worker completed while sleeper was sleeping\n"
        );

        return false;
    }

    if (process != &multi_sleep_test_fixture.process) {
        kernel_panic(
            "Multi-process sleep test received unexpected process"
        );
    }

    if (!multi_worker_completed) {
        kernel_panic(
            "Sleeper completed before multi-process worker"
        );
    }

    if (
        process->id != USER_PROCESS_MULTI_SLEEP_TEST_PID ||
        process->state != PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status !=
            USER_PROCESS_SLEEP_TEST_EXIT_STATUS ||
        process->sleep_start_ticks != 0 ||
        process->sleep_duration_ticks != 0
    ) {
        kernel_panic(
            "Multi-process sleeper did not resume correctly"
        );
    }

    if (!process_reclaim_resources(process)) {
        kernel_panic(
            "Unable to reclaim multi-process sleeper resources"
        );
    }

    if (
        physical_free_frame_count() !=
        sleep_test_free_frame_baseline
    ) {
        kernel_panic(
            "Multi-process sleep test leaked physical frames"
        );
    }

    diagnostics_write(
        "[scheduler] Multi-process sleep/resume test passed\n"
    );

    return true;
}

// Private functions and helpers implementations
static void user_process_sleep_test_prepare_multi(void)
{
    /*
     * Register the sleeper first so it is selected before the worker. When
     * the sleeper blocks, the worker must remain available for immediate
     * scheduling.
     */
    multi_worker_completed = false;

    user_process_fixture_prepare(
        &multi_sleep_test_fixture,
        USER_PROCESS_MULTI_SLEEP_TEST_PID,
        user_program_sleep_probe()
    );

    user_process_fixture_prepare(
        &multi_worker_test_fixture,
        USER_PROCESS_MULTI_WORKER_TEST_PID,
        user_program_survivor()
    );
}

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file lifecycle_stress.c
 * @brief Repeated user-process lifecycle stress regression.
 */

#include "lifecycle_stress.h"
#include "fixture.h"

#include "../../arch/x86_64/tests/user_programs.h"
#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../memory/memory.h"
#include "../../process/process.h"

#include <stddef.h>
#include <stdint.h>

// Defines
#define USER_PROCESS_LIFECYCLE_STRESS_CYCLES 12
#define USER_PROCESS_LIFECYCLE_STRESS_PID_BASE 100

// Static local variables
static struct user_process_fixture lifecycle_stress_fixture;

static size_t lifecycle_stress_cycle;
static uint64_t lifecycle_stress_free_frame_baseline;

// Private functions and helpers declarations
static void user_process_lifecycle_stress_prepare_cycle(void);

// Public functions implementations
void user_process_lifecycle_stress_test_prepare(void)
{
    lifecycle_stress_cycle = 0;

    lifecycle_stress_free_frame_baseline =
        physical_free_frame_count();

    user_process_lifecycle_stress_prepare_cycle();
}

bool user_process_lifecycle_stress_test_terminated(
    struct process *process)
{
    if (process != &lifecycle_stress_fixture.process) {
        kernel_panic(
            "Lifecycle stress handler received unexpected process"
        );
    }

    uint64_t expected_id =
        USER_PROCESS_LIFECYCLE_STRESS_PID_BASE +
        (uint64_t) lifecycle_stress_cycle;

    if (process->id != expected_id) {
        kernel_panic(
            "Lifecycle stress process identifier changed"
        );
    }

    if ((lifecycle_stress_cycle % 2) == 0) {
        if (
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status != 0
        ) {
            kernel_panic(
                "Lifecycle stress normal exit was not preserved"
            );
        }
    } else {
        if (
            process->termination_reason !=
                PROCESS_TERMINATION_SEGMENTATION_FAULT
        ) {
            kernel_panic(
                "Lifecycle stress fault termination was not preserved"
            );
        }
    }

    if (!process_reclaim_resources(process)) {
        kernel_panic(
            "Unable to reclaim lifecycle stress process"
        );
    }

    if (
        physical_free_frame_count() !=
        lifecycle_stress_free_frame_baseline
    ) {
        kernel_panic(
            "Lifecycle stress leaked physical frames"
        );
    }

    ++lifecycle_stress_cycle;

    if (
        lifecycle_stress_cycle <
        USER_PROCESS_LIFECYCLE_STRESS_CYCLES
    ) {
        user_process_lifecycle_stress_prepare_cycle();

        return false;
    }

    diagnostics_write(
        "[process] Repeated process lifecycle stress test passed\n"
    );

    return true;
}

// Private functions and helpers implementations
static void user_process_lifecycle_stress_prepare_cycle(void)
{
    const struct user_program *program;

    if ((lifecycle_stress_cycle % 2) == 0) {
        program =
            user_program_survivor();
    } else {
        program =
            user_program_malicious_page_fault();
    }

    uint64_t process_id =
        USER_PROCESS_LIFECYCLE_STRESS_PID_BASE +
        (uint64_t) lifecycle_stress_cycle;

    diagnostics_printf(
        "[process] Lifecycle stress cycle %u/%u, PID %u\n",
        (uint64_t) lifecycle_stress_cycle + 1,
        (uint64_t) USER_PROCESS_LIFECYCLE_STRESS_CYCLES,
        process_id
    );

    user_process_fixture_prepare(
        &lifecycle_stress_fixture,
        process_id,
        program
    );

    if (
        physical_free_frame_count() >=
        lifecycle_stress_free_frame_baseline
    ) {
        kernel_panic(
            "Lifecycle stress process did not allocate resources"
        );
    }
}

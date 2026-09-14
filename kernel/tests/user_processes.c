// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file user_processes.c
 * @brief User-process scheduler test fixtures.
 */

#include "user_processes.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../memory/memory.h"
#include "../process/layout.h"
#include "../process/memory.h"
#include "../process/process.h"
#include "../arch/x86_64/tests/user_programs.h"
#include "../scheduler/scheduler.h"

#if MYOS_QEMU_TEST_EXIT
#include "../arch/x86_64/qemu_test_exit.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USER_PROCESS_TEST_COUNT 7
#define USER_PROCESS_LIFECYCLE_STRESS_CYCLES 12
#define USER_PROCESS_LIFECYCLE_STRESS_PID_BASE 100

struct user_process_fixture {
    struct process_memory memory;
    struct process_layout layout;
    struct process process;
};

static struct user_process_fixture fixtures[USER_PROCESS_TEST_COUNT];

static struct user_process_fixture lifecycle_stress_fixture;

static size_t lifecycle_stress_cycle;
static uint64_t lifecycle_stress_free_frame_baseline;

static bool standard_process_completed[
    USER_PROCESS_TEST_COUNT
];

static size_t standard_process_completed_count;

// Private helpers declarations
static void user_process_test_prepare(
    struct user_process_fixture *fixture,
    uint64_t id,
    const struct user_program *program
);

static void user_process_tests_prepare_standard(void);

static void user_process_lifecycle_stress_prepare_cycle(void);

static void user_process_lifecycle_stress_terminated(
    struct process *process
);

static void user_process_tests_dump(void);

static void user_process_standard_terminated(
    struct process *process
);

static bool user_process_standard_result_valid(
    size_t index,
    const struct process *process
);
// End private helpers declarations

void user_process_tests_prepare(void)
{
    standard_process_completed_count = 0;

    for (size_t index = 0; index < USER_PROCESS_TEST_COUNT; ++index) {
        standard_process_completed[index] = false;
    }

    lifecycle_stress_cycle = 0;

    lifecycle_stress_free_frame_baseline =
        physical_free_frame_count();

    scheduler_set_terminated_handler(
        user_process_lifecycle_stress_terminated
    );

    user_process_lifecycle_stress_prepare_cycle();
}

static void user_process_tests_prepare_standard(void)
{
    user_process_test_prepare(
        &fixtures[0],
        1,
        user_program_hello()
    );

    user_process_test_prepare(
        &fixtures[1],
        2,
        user_program_counter()
    );

    user_process_test_prepare(
        &fixtures[2],
        3,
        user_program_malicious_page_fault()
    );

    user_process_test_prepare(
        &fixtures[3],
        4,
        user_program_malicious_ud2()
    );

    user_process_test_prepare(
        &fixtures[4],
        5,
        user_program_malicious_hlt()
    );

    user_process_test_prepare(
        &fixtures[5],
        6,
        user_program_survivor()
    );

    user_process_test_prepare(
        &fixtures[6],
        7,
        user_program_malicious_x87()
    );

    user_process_tests_dump();
}

// Private helpers implementations
static void user_process_test_prepare(
    struct user_process_fixture *fixture,
    uint64_t id,
    const struct user_program *program)
{
    if (!process_memory_create(&fixture->memory)) {
        kernel_panic("Unable to create user test process address space");
    }

    if (!process_layout_create(
        &fixture->memory,
        &fixture->layout
    )) {
        kernel_panic("Unable to create user test process layout");
    }

    if (!process_memory_write(
        &fixture->memory,
        fixture->layout.code_base,
        program->data,
        program->size
    )) {
        kernel_panic("Unable to load user test process program");
    }

    if (!process_init(
        &fixture->process,
        id,
        &fixture->memory,
        &fixture->layout
    )) {
        kernel_panic("Unable to initialize user test process");
    }

    if (!scheduler_add(&fixture->process)) {
        kernel_panic("Unable to schedule user test process");
    }
}

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

    user_process_test_prepare(
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

static void user_process_lifecycle_stress_terminated(
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
        return;
    }

    scheduler_set_terminated_handler(
        user_process_standard_terminated
    );

    diagnostics_write(
        "[process] Repeated process lifecycle stress test passed\n"
    );

    user_process_tests_prepare_standard();
}

static void user_process_standard_terminated(
    struct process *process)
{
    if (process == NULL) {
        kernel_panic(
            "Standard process test received null process"
        );
    }

    size_t index = USER_PROCESS_TEST_COUNT;

    for (size_t candidate = 0;
         candidate < USER_PROCESS_TEST_COUNT;
         ++candidate) {
        if (process == &fixtures[candidate].process) {
            index = candidate;
            break;
        }
    }

    if (index == USER_PROCESS_TEST_COUNT) {
        kernel_panic(
            "Standard process test received unexpected process"
        );
    }

    if (standard_process_completed[index]) {
        kernel_panic(
            "Standard process test completed twice"
        );
    }

    if (!user_process_standard_result_valid(
        index,
        process
    )) {
        kernel_panic(
            "Standard process test produced unexpected result"
        );
    }

    standard_process_completed[index] = true;
    ++standard_process_completed_count;

    if (
        standard_process_completed_count <
        USER_PROCESS_TEST_COUNT
    ) {
        return;
    }

    scheduler_set_terminated_handler(NULL);

    diagnostics_write(
        "[test] Kernel test suite passed\n"
    );

#if MYOS_QEMU_TEST_EXIT
    qemu_test_exit_success();
#endif
}

static bool user_process_standard_result_valid(
    size_t index,
    const struct process *process)
{
    if (process->state != PROCESS_STATE_TERMINATED) {
        return false;
    }

    switch (index) {
        case 0:
        case 1:
        case 5:
            return
                process->termination_reason ==
                    PROCESS_TERMINATION_EXITED &&
                process->exit_status == 0;

        case 2:
            return
                process->termination_reason ==
                    PROCESS_TERMINATION_SEGMENTATION_FAULT;

        case 3:
        case 6:
            return
                process->termination_reason ==
                    PROCESS_TERMINATION_ILLEGAL_INSTRUCTION;

        case 4:
            return
                process->termination_reason ==
                    PROCESS_TERMINATION_PROTECTION_FAULT;

        default:
            return false;
    }
}

static void user_process_tests_dump(void)
{
    diagnostics_write("\n--- Initial processes ---\n");

    for (size_t index = 0; index < USER_PROCESS_TEST_COUNT; ++index) {
        const struct user_process_fixture *fixture = &fixtures[index];

        diagnostics_printf(
            "PID %u:\n"
            "  CR3=%x\n"
            "  RIP=%x\n"
            "  RSP=%x\n",
            fixture->process.id,
            fixture->memory.address_space.pml4_physical,
            fixture->layout.entry_point,
            fixture->layout.stack.stack_top
        );
    }

    diagnostics_write("\n");
}

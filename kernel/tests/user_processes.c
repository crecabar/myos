// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file user_processes.c
 * @brief User-process scheduler test fixtures.
 */

#include "user_processes.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../elf/elf64.h"
#include "../memory/memory.h"
#include "../process/create.h"
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

#define USER_PROCESS_LEGACY_TEST_COUNT 7
#define USER_PROCESS_TEST_COUNT 8
#define USER_PROCESS_ELF_TEST_INDEX 7

#define USER_PROCESS_LIFECYCLE_STRESS_CYCLES 12
#define USER_PROCESS_LIFECYCLE_STRESS_PID_BASE 100

#define USER_PROCESS_SLEEP_TEST_PID 112
#define USER_PROCESS_SLEEP_TEST_EXIT_STATUS 42

#define USER_PROCESS_MULTI_SLEEP_TEST_PID 113
#define USER_PROCESS_MULTI_WORKER_TEST_PID 114

#define USER_PROCESS_BLOCK_TEST_PID 115
#define USER_PROCESS_EVENT_WORKER_TEST_PID 116
#define USER_PROCESS_BLOCK_TEST_EXIT_STATUS 43

extern const uint8_t process_elf_entry_fixture_start[];
extern const uint8_t process_elf_entry_fixture_end[];

struct user_process_fixture {
    struct process_memory memory;
    struct process_layout layout;
    struct process process;
};

static struct user_process_fixture fixtures[
    USER_PROCESS_LEGACY_TEST_COUNT
];

static struct user_process_fixture lifecycle_stress_fixture;
static struct user_process_fixture sleep_test_fixture;

static struct user_process_fixture multi_sleep_test_fixture;
static struct user_process_fixture multi_worker_test_fixture;

static struct user_process_fixture block_test_fixture;
static struct user_process_fixture event_worker_test_fixture;

static bool multi_worker_completed;
static bool event_worker_completed;

static size_t lifecycle_stress_cycle;
static uint64_t lifecycle_stress_free_frame_baseline;

static struct process_instance *elf_test_instance;

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

static void user_process_elf_test_prepare(void);

static void user_process_tests_prepare_standard(void);

static void user_process_lifecycle_stress_prepare_cycle(void);

static void user_process_lifecycle_stress_terminated(
    struct process *process
);

static void user_process_sleep_test_terminated(
    struct process *process
);

static void user_process_multi_test_terminated(
    struct process *process
);

static void user_process_block_test_terminated(
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

    user_process_elf_test_prepare();


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

static void user_process_elf_test_prepare(void)
{
    uintptr_t image_start =
        (uintptr_t)
        process_elf_entry_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_elf_entry_fixture_end;

    if (image_end <= image_start) {
        kernel_panic(
            "ELF user test fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "ELF user test fixture is too large"
        );
    }

    size_t image_size = (size_t) image_size_value;

    struct elf64_image image;

    if (!elf64_parse(
        process_elf_entry_fixture_start,
        image_size,
        &image
    )) {
        kernel_panic(
            "Unable to parse ELF user test image"
        );
    }

    const char *argv[] = {
        "elf-entry",
        "argument",
    };

    const char *envp[] = {
        "TERM=myos",
    };

    elf_test_instance = process_create_elf64(
        &image,
        2,
        argv,
        1,
        envp
    );

    if (elf_test_instance == NULL) {
        kernel_panic(
            "Unable to dynamically create ELF user test process"
        );
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

    diagnostics_write(
        "[process] Repeated process lifecycle stress test passed\n"
    );

    /*
     * No standard processes have been registered yet.
     * The sleep probe will therefore be the only runnable
     * process and must pass through scheduler_idle().
     */
    scheduler_set_terminated_handler(
        user_process_sleep_test_terminated
    );

    user_process_test_prepare(
        &sleep_test_fixture,
        USER_PROCESS_SLEEP_TEST_PID,
        user_program_sleep_probe()
    );
}

static void user_process_sleep_test_terminated(
    struct process *process)
{
    if (process != &sleep_test_fixture.process) {
        kernel_panic(
            "Sleep test terminated an unexpected process"
        );
    }

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
        lifecycle_stress_free_frame_baseline
    ) {
        kernel_panic(
            "Sleep test leaked physical frames"
        );
    }

    diagnostics_write(
        "[scheduler] User sleep/resume via idle test passed\n"
    );

    /*
     * The idle regression has released its scheduler slot.
     *
     * Register the sleeper first so it is selected before
     * the worker. When the sleeper blocks, the worker must
     * remain available for immediate scheduling.
     */
    multi_worker_completed = false;

    scheduler_set_terminated_handler(
        user_process_multi_test_terminated
    );

    user_process_test_prepare(
        &multi_sleep_test_fixture,
        USER_PROCESS_MULTI_SLEEP_TEST_PID,
        user_program_sleep_probe()
    );

    user_process_test_prepare(
        &multi_worker_test_fixture,
        USER_PROCESS_MULTI_WORKER_TEST_PID,
        user_program_survivor()
    );
}

static void user_process_multi_test_terminated(
    struct process *process)
{
    /*
     * The worker must finish while the sleeper is still
     * SLEEPING. This proves that the sleeping process did
     * not consume a scheduling turn while the worker ran.
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

        return;
    }

    if (process != &multi_sleep_test_fixture.process) {
        kernel_panic(
            "Multi-process sleep test received unexpected process"
        );
    }

    /*
     * The sleeper must not complete before the worker has
     * executed and terminated.
     */
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
        lifecycle_stress_free_frame_baseline
    ) {
        kernel_panic(
            "Multi-process sleep test leaked physical frames"
        );
    }

    diagnostics_write(
        "[scheduler] Multi-process sleep/resume test passed\n"
    );

    /*
     * Register the blocking process first. Its syscall must
     * transfer execution to the event worker, which remains READY.
     */
    event_worker_completed = false;

    scheduler_set_terminated_handler(
        user_process_block_test_terminated
    );

    user_process_test_prepare(
        &block_test_fixture,
        USER_PROCESS_BLOCK_TEST_PID,
        user_program_block_probe()
    );

    user_process_test_prepare(
        &event_worker_test_fixture,
        USER_PROCESS_EVENT_WORKER_TEST_PID,
        user_program_survivor()
    );
}

static void user_process_block_test_terminated(
    struct process *process)
{
    if (process == &event_worker_test_fixture.process) {
        if (event_worker_completed) {
            kernel_panic(
                "Event worker completed more than once"
            );
        }

        /*
         * The worker's termination is the event that releases
         * the blocked process. Verify its state before waking it.
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
         * The scheduler has detached the terminated worker
         * and activated kernel address space before invoking
         * this handler. The interrupt path still has IF disabled.
         */
        if (!scheduler_wake_blocked(&block_test_fixture.process)) {
            kernel_panic(
                "Unable to wake process after event worker termination"
            );
        }

        if (
            block_test_fixture.process.state !=
                PROCESS_STATE_READY ||
            scheduler_wake_blocked(&block_test_fixture.process)
        ) {
            kernel_panic(
                "Explicit event wakeup violated state transition"
            );
        }

        event_worker_completed = true;

        diagnostics_write(
            "[scheduler] Event worker woke BLOCKED process\n"
        );

        return;
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
        lifecycle_stress_free_frame_baseline
    ) {
        kernel_panic(
            "BLOCKED test leaked physical frames"
        );
    }

    diagnostics_write(
        "[scheduler] User BLOCKED/event wakeup test passed\n"
    );

    /*
     * The event worker and blocked process have both terminated,
     * been detached, and had their resources reclaimed.
     */
    scheduler_set_terminated_handler(
        user_process_standard_terminated
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

    if (
        elf_test_instance != NULL &&
        process == &elf_test_instance->process
    ) {
        if (
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status != 0
        ) {
            kernel_panic(
                "Dynamic ELF user process produced unexpected result"
            );
        }

        if (!process_release_terminated(
            elf_test_instance
        )) {
            kernel_panic(
                "Unable to release dynamic ELF user process"
            );
        }

        elf_test_instance = NULL;

        standard_process_completed[
            USER_PROCESS_ELF_TEST_INDEX
        ] = true;

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

        return;
    }

    for (size_t candidate = 0;
         candidate < USER_PROCESS_LEGACY_TEST_COUNT;
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

    for (
        size_t index = 0;
        index < USER_PROCESS_LEGACY_TEST_COUNT;
        ++index
    ) {
        const struct user_process_fixture *fixture =
            &fixtures[index];

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

    if (elf_test_instance != NULL) {
        diagnostics_printf(
            "PID %u:\n"
            "  CR3=%x\n"
            "  RIP=%x\n"
            "  RSP=%x\n",
            elf_test_instance->process.id,
            elf_test_instance->image.memory.address_space.pml4_physical,
            elf_test_instance->image.layout.entry_point,
            elf_test_instance->image.layout.initial_rsp
        );
    }

    diagnostics_write("\n");
}

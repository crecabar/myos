// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file user_processes.c
 * @brief User-process scheduler test fixtures.
 */

#include "user_processes.h"

#include "../arch/x86_64/paging.h"
#include "../arch/x86_64/tests/user_programs.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../elf/elf64.h"
#include "../memory/heap.h"
#include "../memory/memory.h"
#include "../process/create.h"
#include "../process/exec.h"
#include "../process/layout.h"
#include "../process/memory.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"
#include "../syscall/syscall.h"

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

#define SCHEDULER_CONTEXT_TEST_MIN_PREEMPTIONS 4
#define SCHEDULER_CONTEXT_TEST_MIN_CROSS_ADDRESS_SPACE_PREEMPTIONS 4

extern const uint8_t process_elf_entry_fixture_start[];
extern const uint8_t process_elf_entry_fixture_end[];

extern const uint8_t process_syscall_abi_fixture_start[];
extern const uint8_t process_syscall_abi_fixture_end[];

extern const uint8_t process_syscall_pointer_fixture_start[];
extern const uint8_t process_syscall_pointer_fixture_end[];

extern const uint8_t process_scheduler_context_fixture_start[];
extern const uint8_t process_scheduler_context_fixture_end[];

extern const uint8_t process_exec_caller_fixture_start[];
extern const uint8_t process_exec_caller_fixture_end[];

extern const uint8_t process_exec_target_fixture_start[];
extern const uint8_t process_exec_target_fixture_end[];

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

static struct process_instance
    *syscall_abi_test_instance;

static struct process_instance
    *syscall_pointer_test_instance;

static struct process_instance
    *scheduler_context_test_a_instance;

static struct process_instance
    *scheduler_context_test_b_instance;

static struct process_instance
    *exec_test_instance;

static struct elf64_image exec_test_target_image;

static uint64_t exec_test_free_frame_baseline;
static struct kernel_heap_stats exec_test_heap_baseline;

static uint64_t exec_test_pid;
static uint64_t exec_test_replacement_cr3;

static bool exec_test_replacement_committed;

static uint64_t scheduler_context_test_free_frame_baseline;
static uint64_t scheduler_context_test_preemption_baseline;
static uint64_t scheduler_context_test_cross_address_space_baseline;

static bool scheduler_context_test_a_completed;

static struct paging_translation
    syscall_pointer_kernel_translation_before;

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

static void user_process_syscall_abi_test_prepare(void);

static void user_process_syscall_abi_test_terminated(
    struct process *process
);

static void user_process_syscall_pointer_test_prepare(void);

static void user_process_syscall_pointer_test_terminated(
    struct process *process
);

static void user_process_scheduler_context_test_prepare(void);

static void user_process_scheduler_context_test_terminated(
    struct process *process
);

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

static void user_process_exec_test_prepare(void);

static void user_process_exec_test_terminated(
    struct process *process
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

void user_process_exec_test_syscall(
    struct interrupt_context *context,
    uint64_t operation)
{
    if (context == NULL) {
        kernel_panic(
            "Exec regression received null syscall context"
        );
    }

    if (exec_test_instance == NULL) {
        kernel_panic(
            "Exec regression has no active process"
        );
    }

    struct process *process =
        scheduler_current();

    if (
        process == NULL ||
        process !=
            &exec_test_instance->process ||
        process->id !=
            exec_test_pid
    ) {
        kernel_panic(
            "Exec regression observed incorrect current process"
        );
    }

    uint64_t original_cr3 =
        process
            ->memory
            ->address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    if (operation == 0) {
        uint64_t original_entry_point =
            exec_test_target_image.entry_point;

        exec_test_target_image.entry_point = 0;

        struct process_exec_candidate candidate;

        candidate.prepared = false;

        const char *argv[] = {
            "exec-target",
            "replacement",
        };

        const char *envp[] = {
            "EXEC=1",
        };

        bool prepared =
            process_exec_candidate_prepare_elf64(
                &candidate,
                &exec_test_target_image,
                2,
                argv,
                1,
                envp
            );

        exec_test_target_image.entry_point =
            original_entry_point;

        if (
            prepared ||
            candidate.prepared
        ) {
            kernel_panic(
                "Invalid exec regression target unexpectedly prepared"
            );
        }

        if (
            process->id !=
                exec_test_pid ||
            (
                paging_read_cr3() &
                PAGE_ADDRESS_MASK_4K
            ) != original_cr3
        ) {
            kernel_panic(
                "Failed exec modified the original process"
            );
        }

        context->rax =
            (uint64_t)
            syscall_result_error(
                SYSCALL_ERROR_INVALID_ARGUMENT
            );

        return;
    }

    if (operation != 1) {
        context->rax =
            (uint64_t)
            syscall_result_error(
                SYSCALL_ERROR_INVALID_ARGUMENT
            );

        return;
    }

    struct process_exec_candidate candidate;

    candidate.prepared = false;

    const char *argv[] = {
        "exec-target",
        "replacement",
    };

    const char *envp[] = {
        "EXEC=1",
    };

    if (!process_exec_candidate_prepare_elf64(
        &candidate,
        &exec_test_target_image,
        2,
        argv,
        1,
        envp
    )) {
        kernel_panic(
            "Unable to prepare valid exec regression target"
        );
    }

    exec_test_replacement_cr3 =
        candidate.image
            .memory
            .address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    if (
        exec_test_replacement_cr3 == 0 ||
        exec_test_replacement_cr3 ==
            original_cr3
    ) {
        kernel_panic(
            "Exec regression replacement address space is invalid"
        );
    }

    if (!process_exec_candidate_commit_current(
        process,
        &candidate
    )) {
        kernel_panic(
            "Unable to commit exec regression replacement"
        );
    }

    if (candidate.prepared) {
        kernel_panic(
            "Committed exec regression candidate remained prepared"
        );
    }

    if (
        process->id !=
            exec_test_pid ||
        process->state !=
            PROCESS_STATE_RUNNING
    ) {
        kernel_panic(
            "Exec regression changed process identity"
        );
    }

    exec_test_replacement_committed = true;

    if (!scheduler_load_current_context(
        context
    )) {
        kernel_panic(
            "Unable to load replacement exec context"
        );
    }
}

void user_process_scheduler_context_test_syscall(
    struct interrupt_context *context,
    uint64_t workload)
{
    if (context == NULL) {
        kernel_panic(
            "Scheduler context gate received null syscall context"
        );
    }

    struct process *process =
        scheduler_current();

    if (workload == 0) {
        if (
            scheduler_context_test_a_instance == NULL ||
            process !=
                &scheduler_context_test_a_instance->process
        ) {
            kernel_panic(
                "Scheduler context gate observed incorrect process A"
            );
        }

        uint64_t cross_address_space_preemptions =
            scheduler_test_cross_address_space_preemption_count() -
            scheduler_context_test_cross_address_space_baseline;

        context->rax =
            cross_address_space_preemptions >=
                SCHEDULER_CONTEXT_TEST_MIN_CROSS_ADDRESS_SPACE_PREEMPTIONS
                ? 1
                : 0;

        return;
    }

    if (workload == 1) {
        if (
            scheduler_context_test_b_instance == NULL ||
            process !=
                &scheduler_context_test_b_instance->process
        ) {
            kernel_panic(
                "Scheduler context gate observed incorrect process B"
            );
        }

        context->rax =
            scheduler_context_test_a_completed
                ? 1
                : 0;

        return;
    }

    kernel_panic(
        "Scheduler context gate received invalid workload"
    );
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

static void user_process_syscall_abi_test_prepare(void)
{
    uintptr_t image_start =
        (uintptr_t)
        process_syscall_abi_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_syscall_abi_fixture_end;

    if (image_end <= image_start) {
        kernel_panic(
            "Syscall ABI ELF fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "Syscall ABI ELF fixture is too large"
        );
    }

    size_t image_size =
        (size_t) image_size_value;

    struct elf64_image image;

    if (!elf64_parse(
        process_syscall_abi_fixture_start,
        image_size,
        &image
    )) {
        kernel_panic(
            "Unable to parse syscall ABI ELF fixture"
        );
    }

    const char *argv[] = {
        "syscall-abi",
    };

    syscall_abi_test_instance =
        process_create_elf64(
            &image,
            1,
            argv,
            0,
            NULL
        );

    if (syscall_abi_test_instance == NULL) {
        kernel_panic(
            "Unable to create syscall ABI test process"
        );
    }
}

static void user_process_exec_test_prepare(void)
{
    uintptr_t caller_start =
        (uintptr_t)
        process_exec_caller_fixture_start;

    uintptr_t caller_end =
        (uintptr_t)
        process_exec_caller_fixture_end;

    if (caller_end <= caller_start) {
        kernel_panic(
            "Exec caller fixture has invalid bounds"
        );
    }

    uintptr_t caller_size_value =
        caller_end -
        caller_start;

    if (caller_size_value > SIZE_MAX) {
        kernel_panic(
            "Exec caller fixture is too large"
        );
    }

    struct elf64_image caller_image;

    if (!elf64_parse(
        process_exec_caller_fixture_start,
        (size_t) caller_size_value,
        &caller_image
    )) {
        kernel_panic(
            "Unable to parse exec caller fixture"
        );
    }

    uintptr_t target_start =
        (uintptr_t)
        process_exec_target_fixture_start;

    uintptr_t target_end =
        (uintptr_t)
        process_exec_target_fixture_end;

    if (target_end <= target_start) {
        kernel_panic(
            "Exec target fixture has invalid bounds"
        );
    }

    uintptr_t target_size_value =
        target_end -
        target_start;

    if (target_size_value > SIZE_MAX) {
        kernel_panic(
            "Exec target fixture is too large"
        );
    }

    if (!elf64_parse(
        process_exec_target_fixture_start,
        (size_t) target_size_value,
        &exec_test_target_image
    )) {
        kernel_panic(
            "Unable to parse exec target fixture"
        );
    }

    exec_test_free_frame_baseline =
        physical_free_frame_count();

    if (!kernel_heap_stats_get(
        &exec_test_heap_baseline
    )) {
        kernel_panic(
            "Unable to read heap baseline before exec regression"
        );
    }

    exec_test_replacement_committed = false;
    exec_test_replacement_cr3 = 0;

    const char *argv[] = {
        "exec-caller",
    };

    exec_test_instance =
        process_create_elf64(
            &caller_image,
            1,
            argv,
            0,
            NULL
        );

    if (exec_test_instance == NULL) {
        kernel_panic(
            "Unable to create exec regression process"
        );
    }

    exec_test_pid =
        exec_test_instance->process.id;
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
     /*
     * Before launching the full standard process set, exercise the
     * published syscall ABI from an independently built Ring-3 ELF.
     */
    scheduler_set_terminated_handler(
        user_process_syscall_abi_test_terminated
    );

    user_process_syscall_abi_test_prepare();
}

static void user_process_syscall_pointer_test_prepare(void)
{
    uintptr_t image_start =
        (uintptr_t)
        process_syscall_pointer_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_syscall_pointer_fixture_end;

    if (image_end <= image_start) {
        kernel_panic(
            "Syscall pointer ELF fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "Syscall pointer ELF fixture is too large"
        );
    }

    size_t image_size =
        (size_t) image_size_value;

    struct elf64_image image;

    if (!elf64_parse(
        process_syscall_pointer_fixture_start,
        image_size,
        &image
    )) {
        kernel_panic(
            "Unable to parse syscall pointer ELF fixture"
        );
    }

    const char *argv[] = {
        "syscall-pointer",
    };

    syscall_pointer_test_instance =
        process_create_elf64(
            &image,
            1,
            argv,
            0,
            NULL
        );

    if (syscall_pointer_test_instance == NULL) {
        kernel_panic(
            "Unable to create syscall pointer test process"
        );
    }

    /*
     * Capture one shared higher-half kernel translation before the hostile
     * process executes. The userspace copy path must never modify it.
     *
     * Use a kernel data object's address rather than a function pointer so
     * the conversion to uintptr_t remains ordinary object-pointer arithmetic.
     */
    uint64_t kernel_probe_address =
        (uint64_t) (uintptr_t)
        &lifecycle_stress_free_frame_baseline;

    if (!paging_translate_address_space(
        &syscall_pointer_test_instance
            ->image.memory.address_space,
        kernel_probe_address,
        &syscall_pointer_kernel_translation_before
    )) {
        kernel_panic(
            "Unable to capture syscall pointer kernel mapping"
        );
    }
}

static void user_process_scheduler_context_test_prepare(void)
{
    uintptr_t image_start =
        (uintptr_t)
        process_scheduler_context_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_scheduler_context_fixture_end;

    if (image_end <= image_start) {
        kernel_panic(
            "Scheduler context ELF fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "Scheduler context ELF fixture is too large"
        );
    }

    size_t image_size =
        (size_t) image_size_value;

    struct elf64_image image;

    if (!elf64_parse(
        process_scheduler_context_fixture_start,
        image_size,
        &image
    )) {
        kernel_panic(
            "Unable to parse scheduler context ELF fixture"
        );
    }

    scheduler_context_test_free_frame_baseline =
        physical_free_frame_count();

    scheduler_context_test_preemption_baseline =
        scheduler_test_preemption_count();

    scheduler_context_test_cross_address_space_baseline =
        scheduler_test_cross_address_space_preemption_count();

    scheduler_context_test_a_completed = false;

    const char *argv_a[] = {
        "A",
    };

    scheduler_context_test_a_instance =
        process_create_elf64(
            &image,
            1,
            argv_a,
            0,
            NULL
        );

    if (scheduler_context_test_a_instance == NULL) {
        kernel_panic(
            "Unable to create scheduler context process A"
        );
    }

    const char *argv_b[] = {
        "B",
    };

    scheduler_context_test_b_instance =
        process_create_elf64(
            &image,
            1,
            argv_b,
            0,
            NULL
        );

    if (scheduler_context_test_b_instance == NULL) {
        kernel_panic(
            "Unable to create scheduler context process B"
        );
    }

    uint64_t cr3_a =
        scheduler_context_test_a_instance
            ->image.memory.address_space.pml4_physical;

    uint64_t cr3_b =
        scheduler_context_test_b_instance
            ->image.memory.address_space.pml4_physical;

    if (
        cr3_a == 0 ||
        cr3_b == 0 ||
        cr3_a == cr3_b
    ) {
        kernel_panic(
            "Scheduler context processes do not own independent address spaces"
        );
    }

    /*
     * Both instances deliberately receive equal-sized argv data. Their user
     * stacks should therefore occupy identical virtual addresses while being
     * backed by independent address spaces.
     */
    if (
        scheduler_context_test_a_instance
            ->image.layout.initial_rsp !=
        scheduler_context_test_b_instance
            ->image.layout.initial_rsp
    ) {
        kernel_panic(
            "Scheduler context processes do not share equivalent virtual layout"
        );
    }
}

static void user_process_syscall_abi_test_terminated(
    struct process *process)
{
    if (
        syscall_abi_test_instance == NULL ||
        process !=
            &syscall_abi_test_instance->process
    ) {
        kernel_panic(
            "Syscall ABI test received unexpected process"
        );
    }

    if (
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        kernel_panic(
            "Ring-3 syscall ABI contract test failed"
        );
    }

    if (!process_release_terminated(
        syscall_abi_test_instance
    )) {
        kernel_panic(
            "Unable to release syscall ABI test process"
        );
    }

    syscall_abi_test_instance = NULL;

    if (
        physical_free_frame_count() !=
        lifecycle_stress_free_frame_baseline
    ) {
        kernel_panic(
            "Syscall ABI test leaked physical frames"
        );
    }

    diagnostics_write(
        "[syscall] Ring-3 register and RFLAGS contract test passed\n"
    );

    /*
     * Exercise hostile CPL3 pointers before launching the standard process set.
     */
    scheduler_set_terminated_handler(
        user_process_syscall_pointer_test_terminated
    );

    user_process_syscall_pointer_test_prepare();
}

static void user_process_syscall_pointer_test_terminated(
    struct process *process)
{
    if (
        syscall_pointer_test_instance == NULL ||
        process !=
            &syscall_pointer_test_instance->process
    ) {
        kernel_panic(
            "Syscall pointer test received unexpected process"
        );
    }

    /*
     * Every hostile pointer must have returned BAD_ADDRESS to Ring 3.
     * The userspace fixture exits zero only when every check succeeded.
     *
     * A page fault, protection fault, or any other forced termination is
     * therefore itself a test failure.
     */
    if (
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        kernel_panic(
            "Ring-3 syscall pointer regression test failed"
        );
    }

    uint64_t kernel_probe_address =
        (uint64_t) (uintptr_t)
        &lifecycle_stress_free_frame_baseline;

    struct paging_translation translation_after;

    if (!paging_translate_address_space(
        &syscall_pointer_test_instance
            ->image.memory.address_space,
        kernel_probe_address,
        &translation_after
    )) {
        kernel_panic(
            "Syscall pointer test lost shared kernel mapping"
        );
    }

    if (
        translation_after.physical_address !=
            syscall_pointer_kernel_translation_before
                .physical_address ||
        translation_after.page_size !=
            syscall_pointer_kernel_translation_before
                .page_size ||
        translation_after.pml4_entry !=
            syscall_pointer_kernel_translation_before
                .pml4_entry ||
        translation_after.pdpt_entry !=
            syscall_pointer_kernel_translation_before
                .pdpt_entry ||
        translation_after.pd_entry !=
            syscall_pointer_kernel_translation_before
                .pd_entry ||
        translation_after.pt_entry !=
            syscall_pointer_kernel_translation_before
                .pt_entry
    ) {
        kernel_panic(
            "Syscall pointer test modified shared kernel mapping"
        );
    }

    if (!process_release_terminated(
        syscall_pointer_test_instance
    )) {
        kernel_panic(
            "Unable to release syscall pointer test process"
        );
    }

    syscall_pointer_test_instance = NULL;

    if (
        physical_free_frame_count() !=
        lifecycle_stress_free_frame_baseline
    ) {
        kernel_panic(
            "Syscall pointer test leaked physical frames"
        );
    }

    diagnostics_write(
        "[syscall] Ring-3 invalid user-pointer regressions passed\n"
    );

    scheduler_set_terminated_handler(
        user_process_scheduler_context_test_terminated
    );

    user_process_scheduler_context_test_prepare();
}

static void user_process_exec_test_terminated(
    struct process *process)
{
    if (
        exec_test_instance == NULL ||
        process !=
            &exec_test_instance->process
    ) {
        kernel_panic(
            "Exec regression received unexpected process"
        );
    }

    if (
        !exec_test_replacement_committed ||
        process->id !=
            exec_test_pid ||
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        kernel_panic(
            "Ring-3 exec regression produced unexpected result"
        );
    }

    if (
        process->image == NULL ||
        (
            process->image
                ->memory
                .address_space
                .pml4_physical &
            PAGE_ADDRESS_MASK_4K
        ) != exec_test_replacement_cr3
    ) {
        kernel_panic(
            "Exec regression terminated with incorrect replacement image"
        );
    }

    if (!process_release_terminated(
        exec_test_instance
    )) {
        kernel_panic(
            "Unable to release exec regression process"
        );
    }

    exec_test_instance = NULL;

    if (
        physical_free_frame_count() !=
        exec_test_free_frame_baseline
    ) {
        kernel_panic(
            "Ring-3 exec regression leaked physical frames"
        );
    }

    struct kernel_heap_stats heap_after;

    if (!kernel_heap_stats_get(
        &heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after exec regression"
        );
    }

    if (
        heap_after.allocated_block_count !=
            exec_test_heap_baseline.allocated_block_count ||
        heap_after.allocated_bytes !=
            exec_test_heap_baseline.allocated_bytes
    ) {
        kernel_panic(
            "Ring-3 exec regression leaked kernel heap allocations"
        );
    }

    diagnostics_printf(
        "[process] Ring-3 exec image replacement test passed: PID %u\n",
        exec_test_pid
    );

    scheduler_set_terminated_handler(
        user_process_standard_terminated
    );

    user_process_tests_prepare_standard();
}

static void user_process_scheduler_context_test_terminated(
    struct process *process)
{
    if (
        scheduler_context_test_a_instance != NULL &&
        process ==
            &scheduler_context_test_a_instance->process
    ) {
        if (
            scheduler_context_test_a_completed ||
            process->state !=
                PROCESS_STATE_TERMINATED ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status != 0
        ) {
            kernel_panic(
                "Scheduler context process A produced unexpected result"
            );
        }

        /*
         * A is deliberately the shorter workload. Reaching this point while B
         * is still alive establishes the survivor phase of the regression.
         */
        if (
            scheduler_context_test_b_instance == NULL ||
            scheduler_context_test_b_instance
                ->process.state ==
                PROCESS_STATE_TERMINATED
        ) {
            kernel_panic(
                "Scheduler context process B did not survive process A"
            );
        }

        if (!process_release_terminated(
            scheduler_context_test_a_instance
        )) {
            kernel_panic(
                "Unable to release scheduler context process A"
            );
        }

        scheduler_context_test_a_instance = NULL;
        scheduler_context_test_a_completed = true;

        diagnostics_write(
            "[scheduler] Context process A completed; B survived\n"
        );

        return;
    }

    if (
        scheduler_context_test_b_instance == NULL ||
        process !=
            &scheduler_context_test_b_instance->process
    ) {
        kernel_panic(
            "Scheduler context test received unexpected process"
        );
    }

    if (!scheduler_context_test_a_completed) {
        kernel_panic(
            "Scheduler context process B terminated before A"
        );
    }

    if (
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        kernel_panic(
            "Scheduler context process B produced unexpected result"
        );
    }

    if (!process_release_terminated(
        scheduler_context_test_b_instance
    )) {
        kernel_panic(
            "Unable to release scheduler context process B"
        );
    }

    scheduler_context_test_b_instance = NULL;

    uint64_t preemptions =
        scheduler_test_preemption_count() -
        scheduler_context_test_preemption_baseline;

    uint64_t cross_address_space_preemptions =
        scheduler_test_cross_address_space_preemption_count() -
        scheduler_context_test_cross_address_space_baseline;

    diagnostics_printf(
        "[scheduler] Context-switch observed: "
        "preemptions=%u cross-address-space=%u\n",
        preemptions,
        cross_address_space_preemptions
    );

    if (
        preemptions <
            SCHEDULER_CONTEXT_TEST_MIN_PREEMPTIONS ||
        cross_address_space_preemptions <
            SCHEDULER_CONTEXT_TEST_MIN_CROSS_ADDRESS_SPACE_PREEMPTIONS
    ) {
        kernel_panic(
            "Scheduler context test did not exercise enough preemption"
        );
    }

    if (
        physical_free_frame_count() !=
        scheduler_context_test_free_frame_baseline
    ) {
        kernel_panic(
            "Scheduler context test leaked physical frames"
        );
    }

    diagnostics_printf(
        "[scheduler] Context-switch regression passed: "
        "preemptions=%u cross-address-space=%u\n",
        preemptions,
        cross_address_space_preemptions
    );

    scheduler_set_terminated_handler(
        user_process_exec_test_terminated
    );

    user_process_exec_test_prepare();
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

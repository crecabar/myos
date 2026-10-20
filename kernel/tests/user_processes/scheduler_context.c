// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file scheduler_context.c
 * @brief Ring-3 scheduler context-switch regression fixture.
 */

#include "scheduler_context.h"

#include "../../arch/x86_64/interrupts.h"
#include "../../arch/x86_64/paging.h"
#include <myos/kernel/core/execution/kernel_context.h>
#include <myos/kernel/core/execution/kernel_supervisor.h>
#include <myos/kernel/core/panic.h>
#include "../../diagnostics/diagnostics.h"
#include "../../elf/elf64.h"
#include "../../memory/memory.h"
#include "../../process/create.h"
#include "../../scheduler/scheduler.h"

#include <stddef.h>
#include <stdint.h>

// Defines
#define SCHEDULER_CONTEXT_TEST_MIN_PREEMPTIONS 4
#define SCHEDULER_CONTEXT_TEST_MIN_CROSS_ADDRESS_SPACE_PREEMPTIONS 4
#define SCHEDULER_CONTEXT_HANDOFF_STACK_SIZE 8192U
#define SCHEDULER_CONTEXT_TIMER_VECTOR 0x20U
#define SCHEDULER_CONTEXT_HANDOFF_DISPATCH_COUNT 3U
#define SCHEDULER_CONTEXT_HANDOFF_WAKE_COUNT 2U

#define SCHEDULER_CONTEXT_HANDOFF_STACK_MARKER \
    0x1122334455667788ULL

// Static local variables
static struct process_instance
    *scheduler_context_test_a_instance;

static struct process_instance
    *scheduler_context_test_b_instance;

static uint64_t scheduler_context_test_free_frame_baseline;
static uint64_t scheduler_context_test_preemption_baseline;
static uint64_t scheduler_context_test_cross_address_space_baseline;

static bool scheduler_context_test_a_completed;

extern const uint8_t process_scheduler_context_fixture_start[];
extern const uint8_t process_scheduler_context_fixture_end[];

static _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
unsigned char scheduler_context_handoff_stack[
    SCHEDULER_CONTEXT_HANDOFF_STACK_SIZE
];

static struct kernel_context
    scheduler_context_handoff_worker;

static unsigned int
    scheduler_context_handoff_dispatches;

static unsigned int
    scheduler_context_handoff_wakes;

static unsigned int
    scheduler_context_handoff_entries;

// Private functions and helpers
static void scheduler_context_handoff_worker_entry(
    void *argument);

static void scheduler_context_handoff_wake_if_waiting(void);

// Public functions implementations
void user_process_scheduler_context_test_prepare(void)
{
    uintptr_t image_start =
        (uintptr_t)
        process_scheduler_context_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_scheduler_context_fixture_end;

    scheduler_context_handoff_dispatches = 0U;
    scheduler_context_handoff_wakes = 0U;
    scheduler_context_handoff_entries = 0U;

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

    /*
     * Register a single synthetic kernel worker.
     *
     * The first eligible user-mode timer IRQ must
     * dispatch it through the BSP supervisor.
     */
    if (
        kernel_supervisor_bsp_context() == NULL ||
        !kernel_context_init(
            &scheduler_context_handoff_worker,
            scheduler_context_handoff_stack,
            sizeof(scheduler_context_handoff_stack),
            scheduler_context_handoff_worker_entry,
            &scheduler_context_handoff_worker
        ) ||
        !kernel_context_prepare(
            &scheduler_context_handoff_worker,
            kernel_context_bootstrap
        ) ||
        !kernel_context_bind_supervisor(
            &scheduler_context_handoff_worker,
            kernel_supervisor_bsp_context()
        ) ||
        !scheduler_register_kernel_worker(
            &scheduler_context_handoff_worker
        )
    ) {
        kernel_panic(
            "Unable to register Ring3 timer handoff worker"
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

        scheduler_context_handoff_wake_if_waiting();

        uint64_t cross_address_space_preemptions =
            scheduler_test_cross_address_space_preemption_count() -
            scheduler_context_test_cross_address_space_baseline;

        context->rax =
            cross_address_space_preemptions >=
                SCHEDULER_CONTEXT_TEST_MIN_CROSS_ADDRESS_SPACE_PREEMPTIONS &&
            scheduler_context_handoff_dispatches ==
                SCHEDULER_CONTEXT_HANDOFF_DISPATCH_COUNT &&
            scheduler_context_handoff_wakes ==
                SCHEDULER_CONTEXT_HANDOFF_WAKE_COUNT &&
            scheduler_context_handoff_worker.state ==
                KERNEL_CONTEXT_TERMINATED
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

        scheduler_context_handoff_wake_if_waiting();

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

bool user_process_scheduler_context_test_terminated(
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

        return false;
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

    /*
     * Both user processes have now completed.
     *
     * The worker must have executed exactly once,
     * suspended itself, and released CPU ownership.
     * Its handoff must have been retired before
     * the user processes continued.
     */
    if (
        scheduler_context_handoff_entries != 1U ||
        scheduler_context_handoff_dispatches !=
            SCHEDULER_CONTEXT_HANDOFF_DISPATCH_COUNT ||
        scheduler_context_handoff_wakes !=
            SCHEDULER_CONTEXT_HANDOFF_WAKE_COUNT ||
        scheduler_context_handoff_worker.state !=
            KERNEL_CONTEXT_TERMINATED ||
        kernel_context_owner(
            &scheduler_context_handoff_worker
        ) != KERNEL_CONTEXT_NO_CPU ||
        scheduler_kernel_worker_handoff_pending() ||
        scheduler_kernel_worker_ready()
    ) {
            kernel_panic(
                "Repeated Ring3 timer handoff violated worker lifecycle"
            );
    }

    if (!scheduler_unregister_kernel_worker(
        &scheduler_context_handoff_worker
    )) {
        kernel_panic(
            "Unable to unregister Ring3 timer handoff worker"
        );
    }

    diagnostics_printf(
        "[scheduler] Repeated Ring3 timer handoff passed: "
        "entries=%u dispatches=%u wakes=%u\n",
        scheduler_context_handoff_entries,
        scheduler_context_handoff_dispatches,
        scheduler_context_handoff_wakes
    );

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

    return true;
}

static void scheduler_context_handoff_worker_entry(
    void *argument)
{
    struct kernel_context *worker = argument;

    if (
        worker != &scheduler_context_handoff_worker ||
        scheduler_context_handoff_entries != 0U
    ) {
        kernel_panic(
            "Ring3 handoff worker entered more than once"
        );
    }

    ++scheduler_context_handoff_entries;

    /*
     * This marker lives on the worker's own stack.
     * It must survive both cooperative suspensions.
     */
    volatile uint64_t stack_marker =
        SCHEDULER_CONTEXT_HANDOFF_STACK_MARKER;

    for (
        unsigned int pass = 0U;
        pass < SCHEDULER_CONTEXT_HANDOFF_DISPATCH_COUNT;
        ++pass
    ) {
        struct process *current =
            scheduler_current();

        bool owns_test_process =
            current != NULL &&
            (
                (
                    scheduler_context_test_a_instance != NULL &&
                    current ==
                        &scheduler_context_test_a_instance->process
                ) ||
                (
                    scheduler_context_test_b_instance != NULL &&
                    current ==
                        &scheduler_context_test_b_instance->process
                )
            );

        if (
            !owns_test_process ||
            current->state != PROCESS_STATE_RUNNING ||
            worker->state != KERNEL_CONTEXT_RUNNING ||
            kernel_context_owner(worker) ==
                KERNEL_CONTEXT_NO_CPU ||
            !scheduler_kernel_worker_handoff_pending() ||
            scheduler_context_handoff_dispatches != pass ||
            stack_marker !=
                SCHEDULER_CONTEXT_HANDOFF_STACK_MARKER
        ) {
            kernel_panic(
                "Ring3 repeated handoff worker state invalid"
            );
        }

        uint64_t rflags;

        __asm__ volatile (
            "pushfq\n\t"
            "popq %0"
            : "=r"(rflags)
            :
            : "memory"
        );

        struct paging_address_space *kernel_space =
            paging_kernel_address_space();

        if (
            (rflags & (1ULL << 9)) != 0U ||
            kernel_space == NULL ||
            (paging_read_cr3() & PAGE_ADDRESS_MASK_4K) !=
                kernel_space->pml4_physical
        ) {
            kernel_panic(
                "Ring3 repeated handoff worker CPU state invalid"
            );
        }

        struct scheduler_handoff_resume_plan plan = {0};

        if (
            !scheduler_kernel_worker_resume_preflight(
                &plan
            ) ||
            plan.owner != current ||
            plan.address_space !=
                &current->memory->address_space ||
            plan.frame.vector !=
                SCHEDULER_CONTEXT_TIMER_VECTOR
        ) {
            kernel_panic(
                "Ring3 repeated handoff captured invalid frame"
            );
        }

        ++scheduler_context_handoff_dispatches;

        /*
         * The first two dispatches suspend the worker.
         * The third completes the worker entry normally.
         */
        if (
            pass + 1U <
            SCHEDULER_CONTEXT_HANDOFF_DISPATCH_COUNT
        ) {
            if (!kernel_context_yield_wait(worker)) {
                kernel_panic(
                    "Ring3 repeated handoff worker yield failed"
                );
            }
        }
    }

    /*
     * Returning through kernel_context_bootstrap()
     * transitions the worker to TERMINATED.
     */
}

static void scheduler_context_handoff_wake_if_waiting(void)
{
    /*
     * Only two wakeups are expected.
     *
     * After each dispatch the worker suspends itself.
     * A subsequent syscall may make it READY again.
     */
    if (
        scheduler_context_handoff_wakes >=
            SCHEDULER_CONTEXT_HANDOFF_WAKE_COUNT ||
        scheduler_context_handoff_dispatches !=
            scheduler_context_handoff_wakes + 1U
    ) {
        return;
    }

    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    /*
     * The syscall uses an interrupt gate, so IF must
     * remain disabled while changing worker readiness.
     */
    if (
        (rflags & (1ULL << 9)) != 0U ||
        scheduler_context_handoff_worker.state !=
            KERNEL_CONTEXT_WAITING ||
        scheduler_kernel_worker_handoff_pending() ||
        scheduler_kernel_worker_ready() ||
        kernel_context_owner(
            &scheduler_context_handoff_worker
        ) != KERNEL_CONTEXT_NO_CPU
    ) {
        kernel_panic(
            "Ring3 handoff wakeup observed invalid state"
        );
    }

    if (!kernel_context_wake(
        &scheduler_context_handoff_worker
    )) {
        kernel_panic(
            "Unable to wake Ring3 handoff worker"
        );
    }

    ++scheduler_context_handoff_wakes;

    if (
        scheduler_context_handoff_worker.state !=
            KERNEL_CONTEXT_READY ||
        !scheduler_kernel_worker_ready()
    ) {
        kernel_panic(
            "Ring3 handoff wakeup did not make worker READY"
        );
    }
}

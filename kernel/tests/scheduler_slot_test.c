// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file scheduler_slot_test.c
 * @brief Scheduler registration-slot recycling regression tests.
 */

#include "scheduler_slot_test.h"

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

    scheduler_slot_test_initialize_processes();

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
    }
}

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
#include "../../core/panic.h"
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

// Public functions implementations
void user_process_scheduler_context_test_prepare(void)
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

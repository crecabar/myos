// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file waitpid.c
 * @brief Ring-3 waitpid blocking and reaping regression fixture.
 */

#include "waitpid.h"

#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../elf/elf64.h"
#include "../../memory/heap.h"
#include "../../memory/memory.h"
#include "../../process/create.h"
#include "../../process/process.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Defines
#define USER_PROCESS_WAITPID_CHILD_EXIT_STATUS 37

// Static local variables
static struct process_instance *waitpid_parent_instance;
static struct process_instance *waitpid_child_instance;

static uint64_t waitpid_child_pid;

static uint64_t waitpid_free_frame_baseline;
static struct kernel_heap_stats waitpid_heap_baseline;

static bool waitpid_child_termination_observed;

extern const uint8_t process_waitpid_parent_fixture_start[];
extern const uint8_t process_waitpid_parent_fixture_end[];

extern const uint8_t process_waitpid_child_fixture_start[];
extern const uint8_t process_waitpid_child_fixture_end[];

// Private functions and helpers declarations
static void user_process_waitpid_parse_fixture(
    const uint8_t *start,
    const uint8_t *end,
    struct elf64_image *image
);

// Public functions implementations
void user_process_waitpid_test_prepare(void)
{
    struct elf64_image parent_image;
    struct elf64_image child_image;

    user_process_waitpid_parse_fixture(
        process_waitpid_parent_fixture_start,
        process_waitpid_parent_fixture_end,
        &parent_image
    );

    user_process_waitpid_parse_fixture(
        process_waitpid_child_fixture_start,
        process_waitpid_child_fixture_end,
        &child_image
    );

    waitpid_free_frame_baseline =
        physical_free_frame_count();

    if (!kernel_heap_stats_get(
        &waitpid_heap_baseline
    )) {
        kernel_panic(
            "Unable to read heap baseline before waitpid regression"
        );
    }

    waitpid_parent_instance = NULL;
    waitpid_child_instance = NULL;
    waitpid_child_pid = 0;
    waitpid_child_termination_observed = false;

    const char *parent_argv[] = {
        "waitpid-parent",
    };

    waitpid_parent_instance =
        process_create_elf64(
            &parent_image,
            1,
            parent_argv,
            0,
            NULL
        );

    if (waitpid_parent_instance == NULL) {
        kernel_panic(
            "Unable to create waitpid regression parent"
        );
    }

    const char *child_argv[] = {
        "waitpid-child",
    };

    waitpid_child_instance =
        process_create_child_elf64(
            waitpid_parent_instance,
            &child_image,
            1,
            child_argv,
            0,
            NULL
        );

    if (waitpid_child_instance == NULL) {
        kernel_panic(
            "Unable to create waitpid regression child"
        );
    }

    waitpid_child_pid =
        waitpid_child_instance->process.id;

    if (
        waitpid_child_instance->parent !=
            waitpid_parent_instance ||
        waitpid_parent_instance->first_child !=
            waitpid_child_instance
    ) {
        kernel_panic(
            "Waitpid regression parent-child relationship is incorrect"
        );
    }

    if (
        waitpid_parent_instance->process.state !=
            PROCESS_STATE_READY ||
        waitpid_child_instance->process.state !=
            PROCESS_STATE_READY
    ) {
        kernel_panic(
            "Waitpid regression processes are not ready"
        );
    }

    if (
        waitpid_parent_instance->wait_active ||
        waitpid_parent_instance->wait_child_pid != 0
    ) {
        kernel_panic(
            "Waitpid regression parent began with wait metadata"
        );
    }

    /*
     * The parent regression ELF consumes the direct child PID from RDI.
     * This changes only its initial userspace register state.
     */
    waitpid_parent_instance->process.context.rdi =
        waitpid_child_pid;
}

bool user_process_waitpid_test_terminated(
    struct process *process)
{
    if (
        waitpid_child_instance != NULL &&
        process ==
            &waitpid_child_instance->process
    ) {
        if (waitpid_child_termination_observed) {
            kernel_panic(
                "Waitpid regression child terminated more than once"
            );
        }

        if (
            process->id != waitpid_child_pid ||
            process->state !=
                PROCESS_STATE_TERMINATED ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status !=
                USER_PROCESS_WAITPID_CHILD_EXIT_STATUS
        ) {
            kernel_panic(
                "Waitpid regression child produced unexpected result"
            );
        }

        /*
         * process_lifecycle_notify_terminated() runs before this test
         * handler. The child's termination must therefore already have
         * awakened its BLOCKED parent while preserving the active wait
         * request until userspace actually reaps the child.
         */
        if (
            waitpid_parent_instance == NULL ||
            waitpid_parent_instance->process.state !=
                PROCESS_STATE_READY ||
            !waitpid_parent_instance->wait_active ||
            waitpid_parent_instance->wait_child_pid !=
                waitpid_child_pid ||
            waitpid_parent_instance->first_child !=
                waitpid_child_instance
        ) {
            kernel_panic(
                "Waitpid child termination did not wake parent correctly"
            );
        }

        waitpid_child_termination_observed = true;

        /*
         * Do not release the child here. It deliberately remains linked and
         * terminated until the parent's waitpid syscall consumes it.
         *
         * Drop only the test harness reference so no later code accidentally
         * dereferences the instance after userspace reaps and frees it.
         */
        waitpid_child_instance = NULL;

        diagnostics_write(
            "[process] waitpid child terminated and woke parent\n"
        );

        return false;
    }

    if (
        waitpid_parent_instance == NULL ||
        process !=
            &waitpid_parent_instance->process
    ) {
        kernel_panic(
            "Waitpid regression received unexpected process"
        );
    }

    if (!waitpid_child_termination_observed) {
        kernel_panic(
            "Waitpid parent completed before child termination"
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
            "Ring-3 waitpid regression failed"
        );
    }

    /*
     * The userspace fixture must have consumed the child exactly once and
     * cleared all blocking-wait metadata before exiting.
     */
    if (
        waitpid_parent_instance->first_child != NULL ||
        waitpid_parent_instance->wait_active ||
        waitpid_parent_instance->wait_child_pid != 0
    ) {
        kernel_panic(
            "Waitpid parent retained reaped child lifecycle state"
        );
    }

    if (!process_release_terminated(
        waitpid_parent_instance
    )) {
        kernel_panic(
            "Unable to release waitpid regression parent"
        );
    }

    waitpid_parent_instance = NULL;

    if (
        physical_free_frame_count() !=
        waitpid_free_frame_baseline
    ) {
        kernel_panic(
            "Waitpid regression leaked physical frames"
        );
    }

    struct kernel_heap_stats heap_after;

    if (!kernel_heap_stats_get(
        &heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after waitpid regression"
        );
    }

    if (
        heap_after.allocated_block_count !=
            waitpid_heap_baseline.allocated_block_count ||
        heap_after.allocated_bytes !=
            waitpid_heap_baseline.allocated_bytes
    ) {
        kernel_panic(
            "Waitpid regression leaked kernel heap allocations"
        );
    }

    diagnostics_printf(
        "[process] Ring-3 waitpid blocking/reaping test passed: child PID %u\n",
        waitpid_child_pid
    );

    return true;
}

// Private functions and helpers implementations
static void user_process_waitpid_parse_fixture(
    const uint8_t *start,
    const uint8_t *end,
    struct elf64_image *image)
{
    if (
        start == NULL ||
        end == NULL ||
        image == NULL
    ) {
        kernel_panic(
            "Waitpid regression received invalid ELF fixture"
        );
    }

    uintptr_t image_start =
        (uintptr_t) start;

    uintptr_t image_end =
        (uintptr_t) end;

    if (image_end <= image_start) {
        kernel_panic(
            "Waitpid regression ELF fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "Waitpid regression ELF fixture is too large"
        );
    }

    if (!elf64_parse(
        start,
        (size_t) image_size_value,
        image
    )) {
        kernel_panic(
            "Unable to parse waitpid regression ELF fixture"
        );
    }
}

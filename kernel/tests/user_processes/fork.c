// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file fork.c
 * @brief Ring-3 copy-based fork regression fixture.
 */

#include "fork.h"

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
#define USER_PROCESS_FORK_CHILD_EXIT_STATUS 42

// Static local variables
static struct process_instance *fork_parent_instance;

static uint64_t fork_parent_pid;
static uint64_t fork_child_pid;

static uint64_t fork_free_frame_baseline;
static struct kernel_heap_stats fork_heap_baseline;

static bool fork_child_termination_observed;

extern const uint8_t process_fork_fixture_start[];
extern const uint8_t process_fork_fixture_end[];

// Private functions and helpers declarations
static void user_process_fork_parse_fixture(
    struct elf64_image *image
);

// Public functions implementations
void user_process_fork_test_prepare(void)
{
    struct elf64_image image;

    user_process_fork_parse_fixture(
        &image
    );

    fork_free_frame_baseline =
        physical_free_frame_count();

    if (!kernel_heap_stats_get(
        &fork_heap_baseline
    )) {
        kernel_panic(
            "Unable to read heap baseline before fork regression"
        );
    }

    fork_parent_instance = NULL;
    fork_parent_pid = 0;
    fork_child_pid = 0;

    fork_child_termination_observed =
        false;

    const char *argv[] = {
        "fork-regression",
    };

    fork_parent_instance =
        process_create_elf64(
            &image,
            1,
            argv,
            0,
            NULL
        );

    if (fork_parent_instance == NULL) {
        kernel_panic(
            "Unable to create fork regression parent"
        );
    }

    fork_parent_pid =
        fork_parent_instance->process.id;

    if (
        fork_parent_instance->process.state !=
            PROCESS_STATE_READY ||
        fork_parent_instance->parent != NULL ||
        fork_parent_instance->first_child != NULL ||
        fork_parent_instance->next_sibling != NULL ||
        fork_parent_instance->wait_active ||
        fork_parent_instance->wait_child_pid != 0
    ) {
        kernel_panic(
            "Fork regression parent began with invalid lifecycle state"
        );
    }
}

bool user_process_fork_test_terminated(
    struct process *process)
{
    if (
        process == NULL ||
        process->instance == NULL
    ) {
        kernel_panic(
            "Fork regression received invalid terminated process"
        );
    }

    if (fork_parent_instance == NULL) {
        kernel_panic(
            "Fork regression has no active parent"
        );
    }

    if (
        process !=
        &fork_parent_instance->process
    ) {
        struct process_instance *child =
            process->instance;

        if (fork_child_termination_observed) {
            kernel_panic(
                "Fork regression observed more than one child termination"
            );
        }

        if (
            child->parent !=
                fork_parent_instance ||
            fork_parent_instance->first_child !=
                child ||
            child->process.instance !=
                child
        ) {
            kernel_panic(
                "Fork regression child relationship is incorrect"
            );
        }

        if (
            process->id ==
                fork_parent_pid ||
            process->state !=
                PROCESS_STATE_TERMINATED ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status !=
                USER_PROCESS_FORK_CHILD_EXIT_STATUS
        ) {
            kernel_panic(
                "Fork regression child produced unexpected result"
            );
        }

        fork_child_pid =
            process->id;

        /*
         * A real fork must have created a distinct address space even though
         * parent and child resumed with identical userspace virtual
         * addresses.
         */
        if (
            child->image
                .memory
                .address_space
                .pml4_physical ==
            fork_parent_instance
                ->image
                .memory
                .address_space
                .pml4_physical
        ) {
            kernel_panic(
                "Ring-3 fork child reused parent address space"
            );
        }

        /*
         * process_lifecycle_notify_terminated() executes before this test
         * callback. The parent's blocking wait must therefore already have
         * been satisfied and changed from BLOCKED to READY.
         *
         * The child itself remains linked and waitable until the parent
         * resumes and executes the retried waitpid syscall.
         */
        if (
            fork_parent_instance->process.state !=
                PROCESS_STATE_READY ||
            !fork_parent_instance->wait_active ||
            fork_parent_instance->wait_child_pid !=
                fork_child_pid ||
            fork_parent_instance->first_child !=
                child
        ) {
            kernel_panic(
                "Fork child termination did not wake parent correctly"
            );
        }

        fork_child_termination_observed =
            true;

        diagnostics_printf(
            "[process] fork child PID %u terminated and woke parent\n",
            fork_child_pid
        );

        /*
         * Do not release the child here.
         *
         * The Ring-3 parent owns the wait/reap operation and will free the
         * child through waitpid().
         */
        return false;
    }

    /*
     * Parent completion is valid only after the dynamically created child
     * terminated and was subsequently consumed by waitpid().
     */
    if (!fork_child_termination_observed) {
        kernel_panic(
            "Fork regression parent completed before child"
        );
    }

    if (
        process->id !=
            fork_parent_pid ||
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        kernel_panic(
            "Ring-3 fork regression parent failed"
        );
    }

    /*
     * The userspace fixture performed a second waitpid() for the same PID.
     * Successful exit therefore also proves that the first wait reaped the
     * child exactly once and that the second returned NO_CHILD.
     */
    if (
        fork_parent_instance->first_child != NULL ||
        fork_parent_instance->wait_active ||
        fork_parent_instance->wait_child_pid != 0
    ) {
        kernel_panic(
            "Fork regression parent retained child lifecycle state"
        );
    }

    if (!process_release_terminated(
        fork_parent_instance
    )) {
        kernel_panic(
            "Unable to release fork regression parent"
        );
    }

    fork_parent_instance = NULL;

    if (
        physical_free_frame_count() !=
        fork_free_frame_baseline
    ) {
        kernel_panic(
            "Ring-3 fork regression leaked physical frames"
        );
    }

    struct kernel_heap_stats heap_after;

    if (!kernel_heap_stats_get(
        &heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after fork regression"
        );
    }

    if (
        heap_after.allocated_block_count !=
            fork_heap_baseline.allocated_block_count ||
        heap_after.allocated_bytes !=
            fork_heap_baseline.allocated_bytes
    ) {
        kernel_panic(
            "Ring-3 fork regression leaked kernel heap allocations"
        );
    }

    diagnostics_printf(
        "[process] Ring-3 copy-based fork test passed: "
        "parent PID %u child PID %u\n",
        fork_parent_pid,
        fork_child_pid
    );

    return true;
}

// Private functions and helpers implementations
static void user_process_fork_parse_fixture(
    struct elf64_image *image)
{
    if (image == NULL) {
        kernel_panic(
            "Fork regression received null ELF destination"
        );
    }

    uintptr_t image_start =
        (uintptr_t)
        process_fork_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_fork_fixture_end;

    if (image_end <= image_start) {
        kernel_panic(
            "Fork regression ELF fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "Fork regression ELF fixture is too large"
        );
    }

    if (!elf64_parse(
        process_fork_fixture_start,
        (size_t) image_size_value,
        image
    )) {
        kernel_panic(
            "Unable to parse fork regression ELF fixture"
        );
    }
}

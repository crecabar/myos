// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file commands.c
 * @brief Ring-3 Unix-style userspace command regression.
 */

#include "commands.h"

#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../process/create.h"
#include "../../process/executable.h"
#include "../../process/instance.h"
#include "../../process/process.h"
#include "../../scheduler/scheduler.h"
#include "../../vfs/root.h"
#include "../../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

struct command_test_case {
    const char *path;
    size_t path_length;
    uint64_t expected_exit_status;
};

static const struct command_test_case
command_test_cases[] = {
    {
        "/bin/true",
        sizeof("/bin/true") - 1U,
        0,
    },
    {
        "/bin/false",
        sizeof("/bin/false") - 1U,
        1,
    },
};

#define COMMAND_TEST_COUNT \
    (sizeof(command_test_cases) / sizeof(command_test_cases[0]))

static struct process_instance *
command_test_instance;

static const struct vfs_mount_table *
command_test_mounts;

static size_t command_test_index;

static size_t
command_test_scheduler_count_baseline;

static uint64_t
command_test_pid;

static void command_test_start_current(void);

void user_process_commands_test_prepare(
    const struct vfs_mount_table *mounts)
{
    if (mounts == NULL) {
        kernel_panic(
            "Command test received NULL mount topology"
        );
    }

    if (command_test_instance != NULL) {
        kernel_panic(
            "Command test already has an active process"
        );
    }

    command_test_mounts = mounts;
    command_test_index = 0;

    command_test_scheduler_count_baseline =
        scheduler_test_process_count();

    command_test_start_current();
}

bool user_process_commands_test_terminated(
    struct process *process)
{
    if (
        command_test_instance == NULL ||
        process == NULL ||
        process != &command_test_instance->process ||
        process->id != command_test_pid
    ) {
        kernel_panic(
            "Command test received unexpected process"
        );
    }

    const struct command_test_case *test_case =
        &command_test_cases[command_test_index];

    if (
        process->state != PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status !=
            test_case->expected_exit_status
    ) {
        diagnostics_printf(
            "[userland] Command regression failed: "
            "PID=%u reason=%u status=%u\n",
            process->id,
            (uint64_t) process->termination_reason,
            process->exit_status
        );

        kernel_panic(
            "Userspace command produced unexpected result"
        );
    }

    if (!process_release_terminated(
        command_test_instance
    )) {
        kernel_panic(
            "Unable to release userspace command process"
        );
    }

    command_test_instance = NULL;
    command_test_pid = 0;

    if (
        scheduler_test_process_count() !=
        command_test_scheduler_count_baseline
    ) {
        kernel_panic(
            "Userspace command leaked scheduler registration"
        );
    }

    diagnostics_printf(
        "[userland] %s exited with expected status\n",
        test_case->path
    );

    ++command_test_index;

    if (command_test_index < COMMAND_TEST_COUNT) {
        command_test_start_current();
        return false;
    }

    diagnostics_write(
        "[userland] Unix-style command regression passed\n"
    );

    return true;
}

static void command_test_start_current(void)
{
    if (
        command_test_index >= COMMAND_TEST_COUNT ||
        command_test_instance != NULL
    ) {
        kernel_panic(
            "Invalid userspace command test state"
        );
    }

    struct vfs_node *root = vfs_root_get();

    if (root == NULL) {
        kernel_panic(
            "Command test has no system root"
        );
    }

    const struct command_test_case *test_case =
        &command_test_cases[command_test_index];

    struct process_executable executable = {0};

    if (
        process_executable_open_elf64_from_namespace(
            root,
            command_test_mounts,
            test_case->path,
            test_case->path_length,
            &executable
        ) != PROCESS_EXECUTABLE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to open userspace command executable"
        );
    }

    struct process_initial_vfs initial_vfs = {
        .namespace_root = root,
        .namespace_mounts = command_test_mounts,
    };

    const char *argv[] = {
        test_case->path,
    };

    command_test_instance =
        process_create_elf64_with_vfs(
            &executable.image,
            &initial_vfs,
            1,
            argv,
            0,
            NULL
        );

    if (command_test_instance == NULL) {
        if (!process_executable_close(&executable)) {
            kernel_panic(
                "Unable to close command ELF after creation failure"
            );
        }

        kernel_panic(
            "Unable to create userspace command process"
        );
    }

    command_test_pid = command_test_instance->process.id;

    if (!process_executable_close(&executable)) {
        kernel_panic(
            "Unable to close command ELF after materialization"
        );
    }

    if (
        scheduler_test_process_count() !=
        command_test_scheduler_count_baseline + 1U
    ) {
        kernel_panic(
            "Userspace command was not registered exactly once"
        );
    }
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file runtime_stdio.c
 * @brief Ring-3 bootstrap snprintf/vsnprintf runtime regression.
 */

#include "runtime_stdio.h"

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

static struct process_instance *
runtime_stdio_test_instance;

static uint64_t runtime_stdio_test_pid;

static size_t
runtime_stdio_test_scheduler_count_baseline;

void user_process_runtime_stdio_test_prepare(
    const struct vfs_mount_table *mounts)
{
    if (mounts == NULL) {
        kernel_panic(
            "Runtime stdio test received NULL mount topology"
        );
    }

    if (runtime_stdio_test_instance != NULL) {
        kernel_panic(
            "Runtime stdio test already has an active process"
        );
    }

    runtime_stdio_test_scheduler_count_baseline =
        scheduler_test_process_count();

    struct vfs_node *root =
        vfs_root_get();

    if (root == NULL) {
        kernel_panic(
            "Runtime stdio test has no system root"
        );
    }

    static const char executable_path[] =
        "/bin/runtime-stdio";

    struct process_executable executable = {0};

    enum process_executable_result executable_result =
        process_executable_open_elf64_from_namespace(
            root,
            mounts,
            executable_path,
            sizeof(executable_path) - 1U,
            &executable
        );

    if (
        executable_result !=
        PROCESS_EXECUTABLE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to open runtime stdio test executable"
        );
    }

    const char *argv[] = {
        executable_path,
    };

    runtime_stdio_test_instance =
        process_create_elf64(
            &executable.image,
            1,
            argv,
            0,
            NULL
        );

    if (runtime_stdio_test_instance == NULL) {
        if (!process_executable_close(
            &executable
        )) {
            kernel_panic(
                "Unable to close runtime stdio executable after creation failure"
            );
        }

        kernel_panic(
            "Unable to create runtime stdio test process"
        );
    }

    runtime_stdio_test_pid =
        runtime_stdio_test_instance->process.id;

    if (!process_executable_close(
        &executable
    )) {
        kernel_panic(
            "Unable to close runtime stdio executable after materialization"
        );
    }

    if (
        scheduler_test_process_count() !=
        runtime_stdio_test_scheduler_count_baseline + 1
    ) {
        kernel_panic(
            "Runtime stdio test process was not registered exactly once"
        );
    }
}

void user_process_runtime_stdio_test_terminated(
    struct process *process)
{
    if (
        runtime_stdio_test_instance == NULL ||
        process == NULL ||
        process !=
            &runtime_stdio_test_instance->process
    ) {
        kernel_panic(
            "Runtime stdio test received unexpected process"
        );
    }

    if (
        process->id !=
            runtime_stdio_test_pid ||
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        diagnostics_printf(
            "[runtime] Userspace snprintf/vsnprintf regression failed: "
            "PID=%u reason=%u status=%u\n",
            process->id,
            (uint64_t) process->termination_reason,
            process->exit_status
        );

        kernel_panic(
            "Userspace snprintf/vsnprintf runtime regression failed"
        );
    }

    if (!process_release_terminated(
        runtime_stdio_test_instance
    )) {
        kernel_panic(
            "Unable to release runtime stdio test process"
        );
    }

    runtime_stdio_test_instance =
        NULL;

    if (
        scheduler_test_process_count() !=
        runtime_stdio_test_scheduler_count_baseline
    ) {
        kernel_panic(
            "Runtime stdio test leaked scheduler registration"
        );
    }

    diagnostics_write(
        "[runtime] Userspace snprintf/vsnprintf regression test passed\n"
    );
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file runtime_malloc.c
 * @brief Ring-3 bootstrap malloc/free runtime regression.
 */

#include "runtime_malloc.h"

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
runtime_malloc_test_instance;

static uint64_t runtime_malloc_test_pid;

static size_t
runtime_malloc_test_scheduler_count_baseline;

void user_process_runtime_malloc_test_prepare(
    const struct vfs_mount_table *mounts)
{
    if (mounts == NULL) {
        kernel_panic(
            "Runtime malloc test received NULL mount topology"
        );
    }

    if (runtime_malloc_test_instance != NULL) {
        kernel_panic(
            "Runtime malloc test already has an active process"
        );
    }

    runtime_malloc_test_scheduler_count_baseline =
        scheduler_test_process_count();

    struct vfs_node *root =
        vfs_root_get();

    if (root == NULL) {
        kernel_panic(
            "Runtime malloc test has no system root"
        );
    }

    static const char executable_path[] =
        "/bin/runtime-malloc";

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
            "Unable to open runtime malloc test executable"
        );
    }

    const char *argv[] = {
        executable_path,
    };

    runtime_malloc_test_instance =
        process_create_elf64(
            &executable.image,
            1,
            argv,
            0,
            NULL
        );

    if (runtime_malloc_test_instance == NULL) {
        if (!process_executable_close(
            &executable
        )) {
            kernel_panic(
                "Unable to close runtime malloc executable after creation failure"
            );
        }

        kernel_panic(
            "Unable to create runtime malloc test process"
        );
    }

    runtime_malloc_test_pid =
        runtime_malloc_test_instance->process.id;

    if (!process_executable_close(
        &executable
    )) {
        kernel_panic(
            "Unable to close runtime malloc executable after materialization"
        );
    }

    if (
        scheduler_test_process_count() !=
        runtime_malloc_test_scheduler_count_baseline + 1
    ) {
        kernel_panic(
            "Runtime malloc test process was not registered exactly once"
        );
    }
}

void user_process_runtime_malloc_test_terminated(
    struct process *process)
{
    if (
        runtime_malloc_test_instance == NULL ||
        process == NULL ||
        process !=
            &runtime_malloc_test_instance->process
    ) {
        kernel_panic(
            "Runtime malloc test received unexpected process"
        );
    }

    if (
        process->id !=
            runtime_malloc_test_pid ||
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        diagnostics_printf(
            "[runtime] Userspace malloc/free regression failed: "
            "PID=%u reason=%u status=%u\n",
            process->id,
            (uint64_t) process->termination_reason,
            process->exit_status
        );

        kernel_panic(
            "Userspace malloc/free runtime regression failed"
        );
    }

    if (!process_release_terminated(
        runtime_malloc_test_instance
    )) {
        kernel_panic(
            "Unable to release runtime malloc test process"
        );
    }

    runtime_malloc_test_instance =
        NULL;

    if (
        scheduler_test_process_count() !=
        runtime_malloc_test_scheduler_count_baseline
    ) {
        kernel_panic(
            "Runtime malloc test leaked scheduler registration"
        );
    }

    diagnostics_write(
        "[runtime] Userspace malloc/free regression test passed\n"
    );
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file runtime_syscalls.c
 * @brief Ring-3 native userspace syscall-wrapper regression.
 */

#include "runtime_syscalls.h"

#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../process/create.h"
#include "../../process/executable.h"
#include "../../process/instance.h"
#include "../../process/process.h"
#include "../../scheduler/scheduler.h"
#include "../../vfs/root.h"
#include "../../vfs/vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RUNTIME_SYSCALLS_CHILD_EXIT_STATUS 42

static struct process_instance *
runtime_syscalls_test_instance;

static uint64_t
runtime_syscalls_test_parent_pid;

static uint64_t
runtime_syscalls_test_child_pid;

static size_t
runtime_syscalls_test_scheduler_count_baseline;

static bool
runtime_syscalls_test_child_termination_observed;

void user_process_runtime_syscalls_test_prepare(
    const struct vfs_mount_table *mounts)
{
    if (mounts == NULL) {
        kernel_panic(
            "Runtime syscall test received NULL mount topology"
        );
    }

    if (runtime_syscalls_test_instance != NULL) {
        kernel_panic(
            "Runtime syscall test already has an active process"
        );
    }

    runtime_syscalls_test_scheduler_count_baseline =
        scheduler_test_process_count();

    runtime_syscalls_test_parent_pid = 0;
    runtime_syscalls_test_child_pid = 0;

    runtime_syscalls_test_child_termination_observed =
        false;

    struct vfs_node *root =
        vfs_root_get();

    if (root == NULL) {
        kernel_panic(
            "Runtime syscall test has no system root"
        );
    }

    static const char executable_path[] =
        "/bin/runtime-syscalls";

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
            "Unable to open runtime syscall test executable"
        );
    }

    struct process_initial_vfs initial_vfs = {
        .namespace_root =
            root,
        .namespace_mounts =
            mounts,
    };

    const char *argv[] = {
        executable_path,
    };

    runtime_syscalls_test_instance =
        process_create_elf64_with_vfs(
            &executable.image,
            &initial_vfs,
            1,
            argv,
            0,
            NULL
        );

    if (runtime_syscalls_test_instance == NULL) {
        if (!process_executable_close(
            &executable
        )) {
            kernel_panic(
                "Unable to close runtime syscall executable after creation failure"
            );
        }

        kernel_panic(
            "Unable to create runtime syscall test process"
        );
    }

    runtime_syscalls_test_parent_pid =
        runtime_syscalls_test_instance
            ->process
            .id;

    if (!process_executable_close(
        &executable
    )) {
        kernel_panic(
            "Unable to close runtime syscall executable after materialization"
        );
    }

    if (
        scheduler_test_process_count() !=
        runtime_syscalls_test_scheduler_count_baseline +
            1U
    ) {
        kernel_panic(
            "Runtime syscall test process was not registered exactly once"
        );
    }
}

bool user_process_runtime_syscalls_test_terminated(
    struct process *process)
{
    if (
        runtime_syscalls_test_instance == NULL ||
        process == NULL ||
        process->instance == NULL
    ) {
        kernel_panic(
            "Runtime syscall test received invalid process"
        );
    }

    if (
        process !=
        &runtime_syscalls_test_instance->process
    ) {
        struct process_instance *child =
            process->instance;

        if (
            runtime_syscalls_test_child_termination_observed
        ) {
            kernel_panic(
                "Runtime syscall test observed more than one child termination"
            );
        }

        if (
            child->parent !=
                runtime_syscalls_test_instance ||
            child->process.instance !=
                child ||
            process->id ==
                runtime_syscalls_test_parent_pid
        ) {
            kernel_panic(
                "Runtime syscall test child relationship is invalid"
            );
        }

        if (
            process->state !=
                PROCESS_STATE_TERMINATED ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status !=
                RUNTIME_SYSCALLS_CHILD_EXIT_STATUS
        ) {
            kernel_panic(
                "Runtime syscall test child produced unexpected result"
            );
        }

        runtime_syscalls_test_child_pid =
            process->id;

        runtime_syscalls_test_child_termination_observed =
            true;

        return false;
    }

    if (
        process->id !=
            runtime_syscalls_test_parent_pid ||
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        diagnostics_printf(
            "[runtime] Userspace syscall-wrapper regression failed: "
            "PID=%u reason=%u status=%u\n",
            process->id,
            (uint64_t) process->termination_reason,
            process->exit_status
        );

        kernel_panic(
            "Userspace syscall-wrapper regression failed"
        );
    }

    if (
        !runtime_syscalls_test_child_termination_observed ||
        runtime_syscalls_test_child_pid == 0
    ) {
        kernel_panic(
            "Runtime syscall test parent completed before child observation"
        );
    }

    if (
        runtime_syscalls_test_instance->first_child !=
            NULL ||
        runtime_syscalls_test_instance->wait_active ||
        runtime_syscalls_test_instance->wait_child_pid !=
            0
    ) {
        kernel_panic(
            "Runtime syscall test retained reaped child state"
        );
    }

    if (!process_release_terminated(
        runtime_syscalls_test_instance
    )) {
        kernel_panic(
            "Unable to release runtime syscall test parent"
        );
    }

    runtime_syscalls_test_instance =
        NULL;

    if (
        scheduler_test_process_count() !=
        runtime_syscalls_test_scheduler_count_baseline
    ) {
        kernel_panic(
            "Runtime syscall test leaked scheduler registration"
        );
    }

    diagnostics_printf(
        "[runtime] Userspace syscall-wrapper regression test passed: "
        "parent PID %u child PID %u\n",
        runtime_syscalls_test_parent_pid,
        runtime_syscalls_test_child_pid
    );

    return true;
}

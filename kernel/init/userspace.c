// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file userspace.c
 * @brief Initial userspace bootstrap implementation.
 */

#include "userspace.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/create.h"
#include "../process/executable.h"
#include "../scheduler/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static struct process_instance *
userspace_init_process;

static void userspace_init_terminated_handler(
    struct process *process
);

bool userspace_init_start(
    struct vfs_node *root,
    const struct vfs_mount_table *mounts)
{
    if (
        root == NULL ||
        root->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        userspace_init_process != NULL
    ) {
        return false;
    }

    static const char init_path[] =
        "/init";

    struct process_executable executable = {0};

    enum process_executable_result executable_result =
        process_executable_open_elf64_from_namespace(
            root,
            mounts,
            init_path,
            sizeof(init_path) - 1U,
            &executable
        );

    if (
        executable_result !=
        PROCESS_EXECUTABLE_RESULT_SUCCESS
    ) {
        diagnostics_printf(
            "[init] Unable to open /init executable: result=%u\n",
            (uint64_t) executable_result
        );

        return false;
    }

    const char *const argv[] = {
        init_path,
    };

    struct process_initial_vfs initial_vfs = {
        .namespace_root =
            root,
        .namespace_mounts =
            mounts,
    };

    struct process_instance *instance =
        process_create_elf64_with_vfs(
            &executable.image,
            &initial_vfs,
            1,
            argv,
            0,
            NULL
        );

    if (instance == NULL) {
        if (!process_executable_close(
            &executable
        )) {
            kernel_panic(
                "Unable to release /init executable after creation failure"
            );
        }

        diagnostics_write(
            "[init] Unable to create userspace /init process\n"
        );

        return false;
    }

    /*
     * process_create_elf64_with_vfs() materializes all loadable ELF bytes
     * synchronously. The temporary executable source is therefore no longer
     * required after successful process creation.
     */
    if (!process_executable_close(
        &executable
    )) {
        kernel_panic(
            "Unable to release /init executable after process creation"
        );
    }

    userspace_init_process =
        instance;

    /*
     * Normal userspace process termination flows through the generic lifecycle
     * layer. /init is special only in that its termination means system
     * continuation has been lost and is therefore fatal.
     */
    scheduler_set_terminated_handler(
        userspace_init_terminated_handler
    );

    diagnostics_printf(
        "[init] Scheduled /init as PID %u\n",
        instance->process.id
    );

    return true;
}

static void userspace_init_terminated_handler(
    struct process *process)
{
    if (process == NULL) {
        kernel_panic(
            "Userspace lifecycle received null terminated process"
        );
    }

    if (
        userspace_init_process == NULL ||
        process !=
            &userspace_init_process->process
    ) {
        return;
    }

    /*
     * The scheduler has already detached the process and completed the generic
     * process lifecycle notification before invoking this policy callback.
     * Losing /init means normal userspace continuation no longer exists.
     */
    diagnostics_printf(
        "[init] /init terminated: reason=%u status=%u\n",
        (uint64_t) process->termination_reason,
        process->exit_status
    );

    kernel_panic(
        "Userspace /init terminated unexpectedly"
    );
}

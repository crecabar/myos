// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file lifecycle.c
 * @brief Process lifecycle notifications after scheduler detachment.
 */

#include "lifecycle.h"

#include "cwd.h"
#include "fd_table.h"
#include "namespace.h"
#include "wait.h"

#include "../core/panic.h"

#include <stddef.h>

void process_lifecycle_notify_terminated(
    struct process *process)
{
    if (process == NULL) {
        kernel_panic(
            "Lifecycle received null terminated process"
        );
    }

    if (
        process->state !=
        PROCESS_STATE_TERMINATED
    ) {
        kernel_panic(
            "Lifecycle received non-terminated process"
        );
    }

    if (process->instance == NULL) {
        return;
    }

    if (
        &process->instance->process !=
        process
    ) {
        kernel_panic(
            "Process lifecycle instance link is inconsistent"
        );
    }

    if (!process_fd_table_release_all(
        &process->instance->file_descriptors
    )) {
        kernel_panic(
            "Unable to release terminated process file descriptors"
        );
    }

    if (!process_cwd_release(
        process->instance
    )) {
        kernel_panic(
            "Unable to release terminated process current directory"
        );
    }

    if (!process_namespace_root_release(
        process->instance
    )) {
        kernel_panic(
            "Unable to release terminated process namespace root"
        );
    }

    (void) process_wait_notify_terminated(
        process->instance
    );
}

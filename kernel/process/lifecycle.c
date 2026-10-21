// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file lifecycle.c
 * @brief Process lifecycle notifications after scheduler detachment.
 */

#include "lifecycle.h"

#include <myos/kernel/process/cwd.h>
#include <myos/kernel/process/fd_table.h>
#include <myos/kernel/process/file.h>
#include "namespace.h"
#include "wait.h"

#include <myos/kernel/core/panic.h>

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

    /*
     * A terminated process must not retain callbacks
     * registered against a readable resource.
     *
     * Cancel the process-owned request before releasing
     * descriptor references and namespace resources.
     */
    struct process_instance *instance =
        process->instance;

    if (instance->read_wait_active) {
        if (!process_file_read_wait_cancel(instance)) {
            kernel_panic(
                "Unable to cancel terminated process read wait"
            );
        }
    }

    /*
     * Cancellation must remove both pending registrations
     * and already-delivered logical requests.
     */
    if (
        instance->read_wait_active ||
        instance->read_wait_notified ||
        instance->read_wait.file != NULL ||
        wait_queue_entry_registered(
            &instance->read_wait.entry
        )
    ) {
        kernel_panic(
            "Terminated process retained read wait state"
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

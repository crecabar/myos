// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file fork.c
 * @brief Copy-based process fork lifecycle implementation.
 */

#include "fork.h"

#include "pid.h"

#include "../core/panic.h"
#include "../memory/heap.h"
#include "../scheduler/scheduler.h"

#include <stddef.h>

// Public functions implementations
struct process_instance *process_fork_create_child(
    struct process_instance *parent,
    const struct process_context *parent_context)
{
    if (
        parent == NULL ||
        parent_context == NULL
    ) {
        return NULL;
    }

    if (
        parent->process.instance != parent ||
        parent->process.image !=
            &parent->image ||
        parent->process.memory !=
            &parent->image.memory ||
        parent->process.layout !=
            &parent->image.layout ||
        parent->process.state ==
            PROCESS_STATE_TERMINATED
    ) {
        return NULL;
    }

    uint64_t child_pid;

    if (!process_pid_allocate(
        &child_pid
    )) {
        return NULL;
    }

    struct process_instance *child =
        kmalloc(sizeof(*child));

    if (child == NULL) {
        if (!process_pid_release(
            child_pid
        )) {
            kernel_panic(
                "Unable to roll back fork PID allocation"
            );
        }

        return NULL;
    }

    if (!process_instance_prepare_clone(
        child,
        child_pid,
        parent,
        parent_context
    )) {
        kfree(child);

        if (!process_pid_release(
            child_pid
        )) {
            kernel_panic(
                "Unable to roll back failed fork PID"
            );
        }

        return NULL;
    }

    /*
     * POSIX-style fork result in the child.
     *
     * Every other general-purpose register, RIP, RSP and RFLAGS retain the
     * caller-supplied parent context.
     */
    child->process.context.rax = 0;

    if (!scheduler_add(
        &child->process
    )) {
        if (!process_instance_discard(
            child
        )) {
            kernel_panic(
                "Unable to roll back unscheduled fork child"
            );
        }

        kfree(child);

        if (!process_pid_release(
            child_pid
        )) {
            kernel_panic(
                "Unable to roll back unscheduled fork PID"
            );
        }

        return NULL;
    }

    /*
     * Publication is deliberately last.
     *
     * From this point onward the PID is published and the child belongs to
     * the normal parent/child lifecycle. It must no longer be rolled back as
     * an unpublished creation attempt.
     */
    child->parent = parent;

    child->next_sibling =
        parent->first_child;

    parent->first_child =
        child;

    return child;
}

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file wait.c
 * @brief Parent/child termination observation and process reaping.
 */

#include "wait.h"

#include "create.h"

#include "../core/panic.h"

#include <stddef.h>

// Private functions and helpers declarations
static enum process_wait_result process_wait_reap_child(
    struct process_instance *parent,
    struct process_instance *previous,
    struct process_instance *child,
    struct process_wait_status *status
);

// Public functions implementations
enum process_wait_result process_waitpid_try_reap(
    struct process_instance *parent,
    uint64_t child_pid,
    struct process_wait_status *status)
{
    if (parent == NULL || child_pid == 0) {
        return PROCESS_WAIT_RESULT_INVALID_ARGUMENT;
    }

    if (parent->process.instance != parent) {
        return PROCESS_WAIT_RESULT_INVALID_ARGUMENT;
    }

    struct process_instance *previous = NULL;
    struct process_instance *child =
        parent->first_child;

    while (child != NULL) {
        if (child->process.id == child_pid) {
            if (
                child->process.state !=
                PROCESS_STATE_TERMINATED
            ) {
                return
                    PROCESS_WAIT_RESULT_NOT_TERMINATED;
            }

            return process_wait_reap_child(
                parent,
                previous,
                child,
                status
            );
        }

        previous = child;
        child = child->next_sibling;
    }

    return PROCESS_WAIT_RESULT_NO_CHILD;
}

enum process_wait_result process_wait_try_reap(
    struct process_instance *parent,
    struct process_wait_status *status)
{
    if (parent == NULL) {
        return PROCESS_WAIT_RESULT_INVALID_ARGUMENT;
    }

    if (parent->process.instance != parent) {
        return PROCESS_WAIT_RESULT_INVALID_ARGUMENT;
    }

    struct process_instance *previous = NULL;
    struct process_instance *child =
        parent->first_child;

    if (child == NULL) {
        return PROCESS_WAIT_RESULT_NO_CHILD;
    }

    while (child != NULL) {
        if (
            child->process.state ==
            PROCESS_STATE_TERMINATED
        ) {
            return process_wait_reap_child(
                parent,
                previous,
                child,
                status
            );
        }

        previous = child;
        child = child->next_sibling;
    }

    return PROCESS_WAIT_RESULT_NOT_TERMINATED;
}

// Private functions and helpers implementations
static enum process_wait_result process_wait_reap_child(
    struct process_instance *parent,
    struct process_instance *previous,
    struct process_instance *child,
    struct process_wait_status *status)
{
    if (status != NULL) {
        status->pid = child->process.id;
        status->termination_reason =
            child->process.termination_reason;
        status->exit_status =
            child->process.exit_status;
    }

    if (previous == NULL) {
        parent->first_child =
            child->next_sibling;
    } else {
        previous->next_sibling =
            child->next_sibling;
    }

    child->parent = NULL;
    child->next_sibling = NULL;

    if (!process_release_terminated(
        child
    )) {
        kernel_panic(
            "Unable to release reaped child process"
        );
    }

    return PROCESS_WAIT_RESULT_REAPED;
}

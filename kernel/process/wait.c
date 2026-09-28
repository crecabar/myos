// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file wait.c
 * @brief Parent/child termination observation and process reaping.
 */

#include "wait.h"

#include "create.h"

#include "../core/panic.h"
#include "../scheduler/scheduler.h"

#include <stddef.h>

// Private functions and helpers declarations
static enum process_wait_result process_wait_reap_child(
    struct process_instance *parent,
    struct process_instance *previous,
    struct process_instance *child,
    struct process_wait_status *status
);

static enum process_wait_result process_wait_observe_child(
    const struct process_instance *child,
    struct process_wait_status *status
);

// Public functions implementations
bool process_wait_register(
    struct process_instance *parent,
    uint64_t child_pid)
{
    if (parent == NULL) {
        return false;
    }

    if (
        parent->process.instance != parent ||
        parent->process.state ==
            PROCESS_STATE_TERMINATED ||
        parent->wait_active
    ) {
        return false;
    }

    struct process_instance *child =
        parent->first_child;

    bool matching_child_found = false;

    while (child != NULL) {
        if (
            child_pid == 0 ||
            child->process.id == child_pid
        ) {
            matching_child_found = true;

            /*
             * A terminated child is already immediately waitable.
             * The caller must reap it rather than entering BLOCKED.
             */
            if (
                child->process.state ==
                PROCESS_STATE_TERMINATED
            ) {
                return false;
            }

            if (child_pid != 0) {
                break;
            }
        }

        child = child->next_sibling;
    }

    if (!matching_child_found) {
        return false;
    }

    parent->wait_active = true;
    parent->wait_child_pid = child_pid;

    return true;
}

bool process_wait_cancel(
    struct process_instance *parent)
{
    if (parent == NULL) {
        return false;
    }

    if (
        parent->process.instance != parent ||
        !parent->wait_active
    ) {
        return false;
    }

    parent->wait_active = false;
    parent->wait_child_pid = 0;

    return true;
}

bool process_wait_notify_terminated(
    struct process_instance *child)
{
    if (child == NULL) {
        return false;
    }

    if (
        child->process.instance != child ||
        child->process.state !=
            PROCESS_STATE_TERMINATED
    ) {
        return false;
    }

    struct process_instance *parent =
        child->parent;

    if (parent == NULL) {
        return false;
    }

    if (
        parent->process.instance != parent ||
        !parent->wait_active ||
        parent->process.state !=
            PROCESS_STATE_BLOCKED
    ) {
        return false;
    }

    if (
        parent->wait_child_pid != 0 &&
        parent->wait_child_pid !=
            child->process.id
    ) {
        return false;
    }

    if (!scheduler_wake_blocked(
        &parent->process
    )) {
        kernel_panic(
            "Unable to wake parent waiting for terminated child"
        );
    }

    return true;
}

enum process_wait_result process_waitpid_peek(
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

    struct process_instance *child =
        parent->first_child;

    while (child != NULL) {
        if (child->process.id == child_pid) {
            return process_wait_observe_child(
                child,
                status
            );
        }

        child = child->next_sibling;
    }

    return PROCESS_WAIT_RESULT_NO_CHILD;
}

enum process_wait_result process_wait_peek(
    struct process_instance *parent,
    struct process_wait_status *status)
{
    if (parent == NULL) {
        return PROCESS_WAIT_RESULT_INVALID_ARGUMENT;
    }

    if (parent->process.instance != parent) {
        return PROCESS_WAIT_RESULT_INVALID_ARGUMENT;
    }

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
            return process_wait_observe_child(
                child,
                status
            );
        }

        child = child->next_sibling;
    }

    return PROCESS_WAIT_RESULT_NOT_TERMINATED;
}

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
static enum process_wait_result process_wait_observe_child(
    const struct process_instance *child,
    struct process_wait_status *status)
{
    if (
        child->process.state !=
        PROCESS_STATE_TERMINATED
    ) {
        return PROCESS_WAIT_RESULT_NOT_TERMINATED;
    }

    if (status != NULL) {
        status->pid =
            child->process.id;

        status->termination_reason =
            child->process.termination_reason;

        status->exit_status =
            child->process.exit_status;
    }

    return PROCESS_WAIT_RESULT_TERMINATED;
}

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

    if (
        parent->wait_active &&
        (
            parent->wait_child_pid == 0 ||
            parent->wait_child_pid ==
                child->process.id
        )
    ) {
        parent->wait_active = false;
        parent->wait_child_pid = 0;
    }

    if (!process_release_terminated(
        child
    )) {
        kernel_panic(
            "Unable to release reaped child process"
        );
    }

    return PROCESS_WAIT_RESULT_REAPED;
}

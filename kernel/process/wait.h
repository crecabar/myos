// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file wait.h
 * @brief Parent/child termination observation and process reaping.
 */

#ifndef MYOS_PROCESS_WAIT_H
#define MYOS_PROCESS_WAIT_H

#include "instance.h"

#include <stdint.h>

enum process_wait_result {
    PROCESS_WAIT_RESULT_INVALID_ARGUMENT,
    PROCESS_WAIT_RESULT_NO_CHILD,
    PROCESS_WAIT_RESULT_NOT_TERMINATED,
    PROCESS_WAIT_RESULT_REAPED,
    PROCESS_WAIT_RESULT_TERMINATED,
};

struct process_wait_status {
    uint64_t pid;
    enum process_termination_reason termination_reason;
    uint64_t exit_status;
};

/**
 * Registers a blocking wait request for a direct child.
 *
 * child_pid == 0 selects any direct child.
 *
 * This operation only records the request. It does not change scheduler state;
 * the caller must subsequently block the parent through the scheduler.
 *
 * A terminated matching child must be consumed synchronously through
 * process_waitpid_try_reap() or process_wait_try_reap() instead.
 *
 * @param parent Parent lifecycle instance.
 * @param child_pid Direct child PID, or zero for any child.
 *
 * @return true when the wait request was registered; false otherwise.
 */
bool process_wait_register(
    struct process_instance *parent,
    uint64_t child_pid
);

/**
 * Cancels an active wait request.
 *
 * @param parent Parent lifecycle instance.
 *
 * @return true when an active request was cancelled; false otherwise.
 */
bool process_wait_cancel(
    struct process_instance *parent
);

/**
 * Notifies the wait layer that a child has terminated and been detached.
 *
 * If the direct parent is BLOCKED on this child, or on any child, the parent
 * is made READY through the scheduler.
 *
 * The child is not reaped by this operation. Its termination information
 * remains available until the parent consumes it.
 *
 * @param child Detached terminated child process.
 *
 * @return true when a matching blocked parent was awakened; false otherwise.
 */
bool process_wait_notify_terminated(
    struct process_instance *child
);

/**
 * Observes one direct child without consuming its termination state.
 *
 * @param parent Parent lifecycle instance.
 * @param child_pid Direct child PID.
 * @param status Optional destination for observed termination information.
 *
 * @return TERMINATED when the child is waitable, NOT_TERMINATED while it is
 *         still alive, NO_CHILD when no matching direct child exists, or
 *         INVALID_ARGUMENT for an invalid request.
 */
enum process_wait_result process_waitpid_peek(
    struct process_instance *parent,
    uint64_t child_pid,
    struct process_wait_status *status
);

/**
 * Observes any terminated direct child without consuming it.
 *
 * @param parent Parent lifecycle instance.
 * @param status Optional destination for observed termination information.
 *
 * @return TERMINATED when a child is waitable, NOT_TERMINATED when children
 *         exist but none has terminated, NO_CHILD when there are no direct
 *         children, or INVALID_ARGUMENT for an invalid request.
 */
enum process_wait_result process_wait_peek(
    struct process_instance *parent,
    struct process_wait_status *status
);

enum process_wait_result process_waitpid_try_reap(
    struct process_instance *parent,
    uint64_t child_pid,
    struct process_wait_status *status
);

enum process_wait_result process_wait_try_reap(
    struct process_instance *parent,
    struct process_wait_status *status
);

#endif

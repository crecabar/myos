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
};

struct process_wait_status {
    uint64_t pid;
    enum process_termination_reason termination_reason;
    uint64_t exit_status;
};

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

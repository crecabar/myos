// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_INIT_SUPERVISOR_H
#define MYOS_INIT_SUPERVISOR_H

#include <myos/abi/syscall.h>

#define INIT_SHELL_FAILURE_LIMIT 3U
#define INIT_SHELL_EXIT_STATUS_MAX 255U
#define INIT_SHELL_EXEC_FAILURE_STATUS 256U

struct init_supervisor_state {
    unsigned int consecutive_failures;
};

enum init_supervisor_decision {
    INIT_SUPERVISOR_RESTART,
    INIT_SUPERVISOR_STOP,
};

/**
 * Observes one successfully reaped shell.
 *
 * Any termination EXIT code between 0 and 255
 * is considered normal termination and resets
 * the failure count.
 * Other terminations count toward the limit.
 *
 * No system calls or side effects are performed.
 */
enum init_supervisor_decision
init_supervisor_observe_termination(
    struct init_supervisor_state *state,
    const struct syscall_wait_status *status
);

#endif

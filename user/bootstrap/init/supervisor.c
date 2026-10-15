// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "supervisor.h"

#include <stddef.h>

enum init_supervisor_decision
init_supervisor_observe_termination(
    struct init_supervisor_state *state,
    const struct syscall_wait_status *status)
{
    if (state == NULL || status == NULL) {
        return INIT_SUPERVISOR_STOP;
    }

    if (
        state->consecutive_failures >=
            INIT_SHELL_FAILURE_LIMIT
    ) {
        return INIT_SUPERVISOR_STOP;
    }

    /*
     * The bootstrap shell produces normal exit
     * statuses in the range 0..255.
     *
     * Every such termination completes a session,
     * regardless of its exit status.
     */
    if (
        status->termination_reason ==
            SYSCALL_WAIT_TERMINATION_EXITED &&
        status->exit_status <=
            INIT_SHELL_EXIT_STATUS_MAX
    ) {
        state->consecutive_failures = 0U;

        return INIT_SUPERVISOR_RESTART;
    }

    /*
     * Failed execve() and abnormal process
     * termination count toward the restart limit.
     */
    ++state->consecutive_failures;

    return
        state->consecutive_failures >=
            INIT_SHELL_FAILURE_LIMIT
        ? INIT_SUPERVISOR_STOP
        : INIT_SUPERVISOR_RESTART;
}

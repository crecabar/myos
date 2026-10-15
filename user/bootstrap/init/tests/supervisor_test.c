// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "../supervisor.h"

#include <assert.h>
#include <stddef.h>

static void test_success(void)
{
    struct init_supervisor_state state = {0};

    struct syscall_wait_status status = {
        .termination_reason =
            SYSCALL_WAIT_TERMINATION_EXITED,
        .exit_status = 0U,
    };

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_RESTART
    );

    assert(state.consecutive_failures == 0U);
}

static void test_failure_limit(void)
{
    struct init_supervisor_state state = {0};

    struct syscall_wait_status status = {
        .termination_reason =
            SYSCALL_WAIT_TERMINATION_EXITED,
        .exit_status = INIT_SHELL_EXEC_FAILURE_STATUS,
    };

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_RESTART
    );

    assert(state.consecutive_failures == 1U);

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_RESTART
    );

    assert(state.consecutive_failures == 2U);

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_STOP
    );

    assert(
        state.consecutive_failures ==
            INIT_SHELL_FAILURE_LIMIT
    );

    /*
     * Once stopped, further observations must
     * not overflow or change the failure count.
     */
    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_STOP
    );

    assert(
        state.consecutive_failures ==
            INIT_SHELL_FAILURE_LIMIT
    );
}

static void test_success_resets_failures(void)
{
    struct init_supervisor_state state = {0};

    struct syscall_wait_status status = {
        .termination_reason =
            SYSCALL_WAIT_TERMINATION_EXITED,
        .exit_status =
            INIT_SHELL_EXEC_FAILURE_STATUS,
    };

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_RESTART
    );

    assert(state.consecutive_failures == 1U);

    /*
     * A voluntarily terminated session with a
     * nonzero status is still a normal session.
     */
    status.exit_status = 7U;

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_RESTART
    );

    assert(state.consecutive_failures == 0U);

    status.exit_status =
        INIT_SHELL_EXEC_FAILURE_STATUS;

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_RESTART
    );

    assert(state.consecutive_failures == 1U);

    status.exit_status = 0U;

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_RESTART
    );

    assert(state.consecutive_failures == 0U);
}

static void test_nonzero_session_exits(void)
{
    struct init_supervisor_state state = {0};

    static const uint64_t exit_statuses[] = {
        7U,
        127U,
        255U,
    };

    struct syscall_wait_status status = {
        .termination_reason =
            SYSCALL_WAIT_TERMINATION_EXITED,
    };

    for (
        size_t index = 0U;
        index <
            sizeof(exit_statuses) /
                sizeof(exit_statuses[0]);
        ++index
    ) {
        status.exit_status = exit_statuses[index];

        for (size_t repeat = 0U; repeat < 4U; ++repeat) {
            assert(
                init_supervisor_observe_termination(
                    &state,
                    &status
                ) == INIT_SUPERVISOR_RESTART
            );

            assert(
                state.consecutive_failures == 0U
            );
        }
    }
}

static void test_abnormal_termination(void)
{
    struct init_supervisor_state state = {0};

    struct syscall_wait_status status = {
        .termination_reason =
            SYSCALL_WAIT_TERMINATION_PROTECTION_FAULT,
        .exit_status = 0U,
    };

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_RESTART
    );

    assert(state.consecutive_failures == 1U);

    status.termination_reason =
        SYSCALL_WAIT_TERMINATION_ILLEGAL_INSTRUCTION;

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_RESTART
    );

    assert(state.consecutive_failures == 2U);

    status.termination_reason =
        SYSCALL_WAIT_TERMINATION_SEGMENTATION_FAULT;

    assert(
        init_supervisor_observe_termination(
            &state,
            &status
        ) == INIT_SUPERVISOR_STOP
    );
}

static void test_invalid_arguments(void)
{
    struct init_supervisor_state state = {0};

    struct syscall_wait_status status = {
        .termination_reason =
            SYSCALL_WAIT_TERMINATION_EXITED,
        .exit_status = 0U,
    };

    assert(
        init_supervisor_observe_termination(
            NULL,
            &status
        ) == INIT_SUPERVISOR_STOP
    );

    assert(
        init_supervisor_observe_termination(
            &state,
            NULL
        ) == INIT_SUPERVISOR_STOP
    );

    assert(state.consecutive_failures == 0U);
}

int main(void)
{
    test_success();
    test_nonzero_session_exits();
    test_failure_limit();
    test_success_resets_failures();
    test_abnormal_termination();
    test_invalid_arguments();

    return 0;
}

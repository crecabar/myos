// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file fork.h
 * @brief Ring-3 copy-based fork regression fixture.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_FORK_H
#define MYOS_TESTS_USER_PROCESSES_FORK_H

#include <stdbool.h>

struct process;

/**
 * Prepares the Ring-3 copy-based fork regression.
 */
void user_process_fork_test_prepare(void);

/**
 * Processes termination events belonging to the fork regression.
 *
 * The dynamically forked child must terminate first and wake its parent.
 * The parent then validates the wait result, memory independence and
 * exactly-once child reaping from Ring 3 before terminating successfully.
 *
 * @param process Detached terminated process.
 *
 * @return true when the complete fork regression has finished; false while
 *         another regression process remains active.
 */
bool user_process_fork_test_terminated(
    struct process *process
);

#endif

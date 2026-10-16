// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file waitpid.h
 * @brief Ring-3 waitpid blocking and reaping regression fixture.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_WAITPID_H
#define MYOS_TESTS_USER_PROCESSES_WAITPID_H

#include <stdbool.h>

struct process;

/**
 * Prepares the Ring-3 parent/child waitpid regression.
 */
void user_process_waitpid_test_prepare(void);

/**
 * Processes termination events belonging to the waitpid regression.
 *
 * Child termination validates the lifecycle wakeup and keeps the child
 * available for userspace reaping. Parent termination validates the final
 * exactly-once reaping and resource-lifecycle invariants.
 *
 * @param process Detached terminated process.
 *
 * @return true when the complete waitpid regression has finished; false while
 *         another regression process remains active.
 */
bool user_process_waitpid_test_terminated(
    struct process *process
);

#endif

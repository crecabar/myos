// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file sleep.h
 * @brief Ring-3 scheduler sleep/resume regression fixtures.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_SLEEP_H
#define MYOS_TESTS_USER_PROCESSES_SLEEP_H

#include <stdbool.h>

struct process;

/**
 * Prepares the single-process sleep/resume regression.
 *
 * The first process is deliberately the only runnable process so its sleep
 * must pass through scheduler idle.
 */
void user_process_sleep_test_prepare(void);

/**
 * Processes termination events belonging to the sleep regressions.
 *
 * The regression first exercises sleep/resume through scheduler idle and then
 * verifies that another READY process can execute while a sleeper remains
 * unavailable to the scheduler.
 *
 * @param process Detached terminated process.
 *
 * @return true when both sleep regressions have completed; false while another
 * sleep-regression process remains active.
 */
bool user_process_sleep_test_terminated(
    struct process *process
);

#endif

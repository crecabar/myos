// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file block.h
 * @brief Ring-3 explicit BLOCKED/wakeup regression fixture.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_BLOCK_H
#define MYOS_TESTS_USER_PROCESSES_BLOCK_H

#include <stdbool.h>

struct process;

/**
 * Prepares the explicit BLOCKED/event wakeup regression.
 */
void user_process_block_test_prepare(void);

/**
 * Processes termination events belonging to the BLOCKED regression.
 *
 * @param process Detached terminated process.
 *
 * @return true when the complete BLOCKED regression has finished; false while
 * another regression process remains active.
 */
bool user_process_block_test_terminated(
    struct process *process
);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file standard.h
 * @brief Standard Ring-3 process regression fixtures.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_STANDARD_H
#define MYOS_TESTS_USER_PROCESSES_STANDARD_H

struct process;

/**
 * Prepares the standard built-in and ELF-backed Ring-3 process regressions.
 */
void user_process_standard_test_prepare(void);

/**
 * Processes termination of one standard regression process.
 *
 * The final completion disables the scheduler termination handler and marks
 * the complete kernel test suite successful.
 *
 * @param process Detached terminated process.
 */
void user_process_standard_test_terminated(
    struct process *process
);

#endif

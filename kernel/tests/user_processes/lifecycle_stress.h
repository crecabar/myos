// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file lifecycle_stress.h
 * @brief Repeated user-process lifecycle stress regression.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_LIFECYCLE_STRESS_H
#define MYOS_TESTS_USER_PROCESSES_LIFECYCLE_STRESS_H

#include <stdbool.h>

struct process;

/**
 * Prepares the repeated process lifecycle stress regression.
 */
void user_process_lifecycle_stress_test_prepare(void);

/**
 * Processes one terminated lifecycle-stress process.
 *
 * @param process Detached terminated process.
 *
 * @return true when all lifecycle stress cycles have completed; false when
 * another cycle has been scheduled.
 */
bool user_process_lifecycle_stress_test_terminated(
    struct process *process
);

#endif

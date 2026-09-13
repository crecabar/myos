// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file user_processes.h
 * @brief User-process scheduler test fixtures.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_H
#define MYOS_TESTS_USER_PROCESSES_H

/**
 * Prepares the built-in user processes used to exercise the scheduler.
 *
 * The created processes and their address spaces remain alive for the entire
 * kernel lifetime.
 */
void user_process_tests_prepare(void);

#endif

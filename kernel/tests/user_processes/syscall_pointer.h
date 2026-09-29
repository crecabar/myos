// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file syscall_pointer.h
 * @brief Ring-3 hostile userspace pointer regression fixture.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_SYSCALL_POINTER_H
#define MYOS_TESTS_USER_PROCESSES_SYSCALL_POINTER_H

struct process;

/**
 * Prepares the Ring-3 hostile userspace pointer regression.
 */
void user_process_syscall_pointer_test_prepare(void);

/**
 * Validates and releases the terminated hostile pointer regression process.
 *
 * @param process Detached terminated process.
 */
void user_process_syscall_pointer_test_terminated(
    struct process *process
);

#endif

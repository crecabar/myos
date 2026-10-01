// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file fd_syscalls.h
 * @brief Ring-3 descriptor syscall regression fixture.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_FD_SYSCALLS_H
#define MYOS_TESTS_USER_PROCESSES_FD_SYSCALLS_H

struct process;

/**
 * Creates and configures the Ring-3 descriptor syscall regression process.
 */
void user_process_fd_syscalls_test_prepare(void);

/**
 * Validates and releases the terminated descriptor syscall regression process.
 *
 * @param process Detached terminated process.
 */
void user_process_fd_syscalls_test_terminated(
    struct process *process
);

#endif

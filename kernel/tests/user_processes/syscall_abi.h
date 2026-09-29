// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file syscall_abi.h
 * @brief Ring-3 syscall ABI regression fixture.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_SYSCALL_ABI_H
#define MYOS_TESTS_USER_PROCESSES_SYSCALL_ABI_H

struct process;

/**
 * Prepares the Ring-3 syscall ABI regression.
 */
void user_process_syscall_abi_test_prepare(void);

/**
 * Validates and releases the terminated syscall ABI regression process.
 *
 * @param process Detached terminated process.
 */
void user_process_syscall_abi_test_terminated(
    struct process *process
);

#endif

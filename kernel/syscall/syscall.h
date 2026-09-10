// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file syscall.h
 * @brief MyOS system call dispatch interface.
 */

#ifndef MYOS_SYSCALL_SYSCALL_H
#define MYOS_SYSCALL_SYSCALL_H

#include <stdint.h>

/**
 * Debug system call that writes one character through kernel diagnostics.
 */
#define SYSCALL_DEBUG_PUTC 1
#define SYSCALL_EXIT       2
#define SYSCALL_YIELD      3

/**
 * Dispatches one system call requested by user mode.
 *
 * Some system calls, such as process exit, transfer control to the scheduler
 * and therefore do not return to the calling process.
 *
 * @param number System call number.
 * @param argument0 First system call argument.
 *
 * @return System call result returned to user mode when the call returns.
 */
uint64_t syscall_dispatch(uint64_t number, uint64_t argument0);

#endif

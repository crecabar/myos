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
#define SYSCALL_DEBUG_PUTC 1    // RDI = character
#define SYSCALL_EXIT       2    // RDI = status
#define SYSCALL_YIELD      3    // no arguments
#define SYSCALL_WRITE      4    // RDI = buffer, RSI = length

/**
 * Dispatches one system call requested by user mode.
 *
 * MyOS uses the x86-64 system call register convention:
 *
 *   RAX = system call number
 *   RDI = argument 0
 *   RSI = argument 1
 *   RDX = argument 2
 *   R10 = argument 3
 *   R8  = argument 4
 *   R9  = argument 5
 *
 * The system call result is returned in RAX.
 *
 * @param number System call number.
 * @param argument0 First system call argument.
 * @param argument1 Second system call argument.
 * @param argument2 Third system call argument.
 * @param argument3 Fourth system call argument.
 * @param argument4 Fifth system call argument.
 * @param argument5 Sixth system call argument.
 *
 * @return System call result returned to user mode.
 */
uint64_t syscall_dispatch(
    uint64_t number,
    uint64_t argument0,
    uint64_t argument1,
    uint64_t argument2,
    uint64_t argument3,
    uint64_t argument4,
    uint64_t argument5
);

#endif

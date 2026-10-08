// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process.h
 * @brief Native bootstrap process syscall wrappers.
 */

#ifndef MYOS_USER_RUNTIME_MYOS_PROCESS_H
#define MYOS_USER_RUNTIME_MYOS_PROCESS_H

#include <myos/abi/syscall.h>

#include <stdint.h>

/**
 * Creates an independent child copy of the current process.
 *
 * On success the parent receives the child PID and the child receives zero.
 * Errors are returned as negative enum syscall_error values.
 *
 * @return Child PID in the parent, zero in the child, or a negative error.
 */
syscall_result_t myos_fork(void);

/**
 * Waits for one direct child process.
 *
 * child_pid == 0 selects any direct child. status may be NULL when termination
 * details are not required. options uses the published SYSCALL_WAITPID_*
 * flags.
 *
 * @param child_pid Direct child PID, or zero for any child.
 * @param status Optional termination-status destination.
 * @param options Published wait option flags.
 *
 * @return Reaped child PID, zero for a successful non-blocking no-result
 *         operation, or a negative error.
 */
syscall_result_t myos_waitpid(
    uint64_t child_pid,
    struct syscall_wait_status *status,
    uint64_t options
);

#endif

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

#include <stddef.h>
#include <stdint.h>

/**
 * Returns the identifier of the calling process.
 *
 * @return Positive process identifier on success, or a negative error.
 */
syscall_result_t myos_getpid(void);

/**
 * Changes the calling process current working directory.
 *
 * path is an explicit byte sequence and need not be NUL-terminated.
 *
 * @param path Pathname bytes.
 * @param path_length Number of pathname bytes.
 *
 * @return Zero on success, or a negative error.
 */
syscall_result_t myos_chdir(
    const char *path,
    size_t path_length
);

/**
 * Copies the calling process current working directory pathname.
 *
 * capacity includes space for the terminating NUL byte.
 *
 * On success, buffer contains one NUL-terminated absolute pathname and the
 * return value is its length excluding the terminating NUL.
 *
 * @param buffer Writable userspace pathname destination.
 * @param capacity Number of bytes available in buffer.
 *
 * @return Pathname length excluding NUL on success, or a negative error.
 */
syscall_result_t myos_getcwd(
    char *buffer,
    size_t capacity
);

/**
 * Replaces the calling process image with one ELF executable.
 *
 * path is an explicit byte sequence and need not be NUL-terminated.
 *
 * argv and envp contain userspace pointers to NUL-terminated strings. Their
 * element counts are supplied explicitly and therefore the vectors themselves
 * require no trailing NULL pointer.
 *
 * On success this function does not return: execution resumes at the entry
 * point of the replacement executable with a newly constructed initial stack.
 *
 * Process identity, file descriptors, namespace root and current working
 * directory remain associated with the calling process.
 *
 * @param path Executable pathname bytes.
 * @param path_length Number of pathname bytes.
 * @param argv Argument string vector.
 * @param argc Number of argv entries.
 * @param envp Environment string vector.
 * @param envc Number of envp entries.
 *
 * @return A negative syscall error when replacement cannot be performed.
 */
syscall_result_t myos_execve(
    const char *path,
    size_t path_length,
    const char *const argv[],
    size_t argc,
    const char *const envp[],
    size_t envc
);

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

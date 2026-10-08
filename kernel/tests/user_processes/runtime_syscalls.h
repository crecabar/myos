// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file runtime_syscalls.h
 * @brief Ring-3 native userspace syscall-wrapper regression.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_RUNTIME_SYSCALLS_H
#define MYOS_TESTS_USER_PROCESSES_RUNTIME_SYSCALLS_H

#include <stdbool.h>

struct process;
struct vfs_mount_table;

/**
 * Creates the native userspace syscall-wrapper regression process.
 *
 * The process receives the normal system namespace and root CWD so the test
 * exercises descriptor pathname operations through the same VFS context used
 * by bootstrap userspace.
 *
 * @param mounts Borrowed kernel-owned mount topology.
 */
void user_process_runtime_syscalls_test_prepare(
    const struct vfs_mount_table *mounts
);

/**
 * Processes one termination notification belonging to the regression.
 *
 * The forked child remains owned by the parent until waitpid() reaps it.
 * Therefore child termination does not complete the regression.
 *
 * @param process Detached terminated process.
 *
 * @return true after the parent completed successfully; false after the child
 *         terminated and the parent must continue running.
 */
bool user_process_runtime_syscalls_test_terminated(
    struct process *process
);

#endif

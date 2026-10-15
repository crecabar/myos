// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file blocking_read.h
 * @brief Ring-3 blocking descriptor read regression.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_BLOCKING_READ_H
#define MYOS_TESTS_USER_PROCESSES_BLOCKING_READ_H

#include <stdbool.h>

struct process;
struct vfs_mount_table;

/**
 * Prepares the reader, writer and shared VFS fixture.
 */
void user_process_blocking_read_test_prepare(
    const struct vfs_mount_table *mounts
);

/**
 * Observes termination of the two test processes.
 *
 * Returns true only after both have completed and
 * all test-owned resources have been released.
 */
bool user_process_blocking_read_test_terminated(
    struct process *process
);

#endif

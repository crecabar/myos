// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file runtime_malloc.h
 * @brief Ring-3 bootstrap malloc/free runtime regression.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_RUNTIME_MALLOC_H
#define MYOS_TESTS_USER_PROCESSES_RUNTIME_MALLOC_H

struct process;
struct vfs_mount_table;

/**
 * Creates the bootstrap malloc/free regression process.
 *
 * The executable is resolved from /bin/runtime-malloc through the supplied
 * kernel-owned mount topology and materialized before execution begins.
 *
 * @param mounts Borrowed kernel-owned mount topology.
 */
void user_process_runtime_malloc_test_prepare(
    const struct vfs_mount_table *mounts
);

/**
 * Validates and releases the completed malloc/free regression process.
 *
 * @param process Detached terminated process.
 */
void user_process_runtime_malloc_test_terminated(
    struct process *process
);

#endif

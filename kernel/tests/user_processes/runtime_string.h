// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file runtime_string.h
 * @brief Ring-3 bootstrap memory and string runtime regression.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_RUNTIME_STRING_H
#define MYOS_TESTS_USER_PROCESSES_RUNTIME_STRING_H

struct process;
struct vfs_mount_table;

/**
 * Creates the bootstrap runtime string regression process.
 *
 * The executable is resolved from /bin/runtime-string through the supplied
 * kernel-owned mount topology and materialized before execution begins.
 *
 * @param mounts Borrowed kernel-owned mount topology.
 */
void user_process_runtime_string_test_prepare(
    const struct vfs_mount_table *mounts
);

/**
 * Validates and releases the completed runtime string regression process.
 *
 * @param process Detached terminated process.
 */
void user_process_runtime_string_test_terminated(
    struct process *process
);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file user_processes.h
 * @brief User-process scheduler test fixtures.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_H
#define MYOS_TESTS_USER_PROCESSES_H

struct vfs_mount_table;

/**
 * Prepares the built-in user processes used to exercise the scheduler.
 *
 * The supplied mount topology is borrowed from the kernel for VFS-backed
 * userspace regressions and remains kernel-owned.
 *
 * @param mounts Borrowed kernel-owned mount topology.
 */
void user_process_tests_prepare(
    const struct vfs_mount_table *mounts
);

#endif

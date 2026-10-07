// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file vfs_elf.h
 * @brief Ring-3 execution regression for an ELF loaded from VFS.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_VFS_ELF_H
#define MYOS_TESTS_USER_PROCESSES_VFS_ELF_H

struct process;
struct vfs_mount_table;

/**
 * Creates the Ring-3 regression process from /bin/elf-from-vfs.
 *
 * The executable is resolved through the supplied kernel-owned mount topology,
 * fully materialized into a process image, and closed before execution begins.
 *
 * @param mounts Borrowed kernel-owned mount topology.
 */
void user_process_vfs_elf_test_prepare(
    const struct vfs_mount_table *mounts
);

/**
 * Validates and releases the completed VFS-backed Ring-3 process.
 *
 * @param process Detached terminated process.
 */
void user_process_vfs_elf_test_terminated(
    struct process *process
);

#endif

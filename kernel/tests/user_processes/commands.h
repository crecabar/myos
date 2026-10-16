// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file commands.h
 * @brief Ring-3 Unix-style userspace command regression.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_COMMANDS_H
#define MYOS_TESTS_USER_PROCESSES_COMMANDS_H

#include <stdbool.h>

struct process;
struct vfs_mount_table;

/**
 * Starts the userspace command regression sequence.
 *
 * Executes installed commands through the normal VFS and ELF
 * process creation path.
 *
 * @param mounts Borrowed system mount topology.
 */
void user_process_commands_test_prepare(
    const struct vfs_mount_table *mounts
);

/**
 * Validates one command termination and advances the sequence.
 *
 * @param process Detached terminated process.
 *
 * @return true when all commands completed successfully;
 *         false when another command was started.
 */
bool user_process_commands_test_terminated(
    struct process *process
);

#endif

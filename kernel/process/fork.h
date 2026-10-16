// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file fork.h
 * @brief Copy-based process fork lifecycle operation.
 */

#ifndef MYOS_PROCESS_FORK_H
#define MYOS_PROCESS_FORK_H

#include "instance.h"

#include <stdint.h>

/**
 * Creates and schedules an independent child clone of a process.
 *
 * The child receives:
 *
 * - a new process identifier;
 * - an independent copy of the parent's userspace image;
 * - the supplied parent execution context;
 * - RAX = 0 as the fork return value;
 * - a direct lifecycle relationship to parent.
 *
 * The parent process and supplied context are not modified.
 *
 * The child is published in the parent's child list only after complete image
 * cloning and scheduler registration succeed.
 *
 * On failure, PID allocation, image resources and lifecycle storage are
 * completely rolled back.
 *
 * @param parent Existing process instance.
 * @param parent_context Parent execution context immediately after the fork
 *        syscall instruction.
 *
 * @return Scheduled child instance on success; NULL on failure.
 */
struct process_instance *process_fork_create_child(
    struct process_instance *parent,
    const struct process_context *parent_context
);

#endif

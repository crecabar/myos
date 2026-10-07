// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file pid.c
 * @brief Minimal process identifier allocation implementation.
 */

#include "pid.h"

#include <stddef.h>
#include <stdint.h>

/*
 * PID allocation is initialized explicitly by boot policy.
 *
 * Keeping the initial identifier outside this module prevents the allocator
 * from depending on build-time test configuration. Normal boot currently
 * starts at PID 1, while test boot may select a compatibility range for
 * legacy fixtures that still assign identifiers directly.
 */
static uint64_t next_process_pid;
static bool process_pid_initialized;

bool process_pid_initialize(
    uint64_t first_pid)
{
    if (
        process_pid_initialized ||
        first_pid == 0
    ) {
        return false;
    }

    next_process_pid =
        first_pid;

    process_pid_initialized =
        true;

    return true;
}

bool process_pid_allocate(uint64_t *pid)
{
    if (
        pid == NULL ||
        !process_pid_initialized
    ) {
        return false;
    }

    if (next_process_pid == 0) {
        return false;
    }

    *pid = next_process_pid;

    ++next_process_pid;

    return true;
}

bool process_pid_release(uint64_t pid)
{
    if (
        !process_pid_initialized ||
        pid == 0
    ) {
        return false;
    }

    if (next_process_pid == 0) {
        if (pid != UINT64_MAX) {
            return false;
        }

        next_process_pid = UINT64_MAX;

        return true;
    }

    if (pid != next_process_pid - 1) {
        return false;
    }

    next_process_pid = pid;

    return true;
}

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file pid.c
 * @brief Minimal process identifier allocation implementation.
 */

#include "pid.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Dynamic PID allocation currently starts above the fixed legacy test ranges.
 * The temporary offset can be removed once all process creation paths use the
 * central allocator.
 */
static uint64_t next_process_pid = 1000;

bool process_pid_allocate(uint64_t *pid)
{
    if (pid == NULL) return false;

    if (next_process_pid == 0) {
        return false;
    }

    *pid = next_process_pid;

    ++next_process_pid;

    return true;
}

bool process_pid_release(uint64_t pid)
{
    if (pid == 0) return false;

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

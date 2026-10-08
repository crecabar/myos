// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process.c
 * @brief Native bootstrap process syscall wrappers.
 */

#include <myos/process.h>

#include "syscall.h"

#include <stdint.h>

syscall_result_t myos_fork(void)
{
    return __myos_syscall6(
        SYSCALL_FORK,
        0,
        0,
        0,
        0,
        0,
        0
    );
}

syscall_result_t myos_waitpid(
    uint64_t child_pid,
    struct syscall_wait_status *status,
    uint64_t options)
{
    return __myos_syscall6(
        SYSCALL_WAITPID,
        child_pid,
        (uint64_t)
            (uintptr_t) status,
        options,
        0,
        0,
        0
    );
}

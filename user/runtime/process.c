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

syscall_result_t myos_getpid(void)
{
    return __myos_syscall6(
        SYSCALL_GETPID,
        0,
        0,
        0,
        0,
        0,
        0
    );
}

syscall_result_t myos_chdir(
    const char *path,
    size_t path_length)
{
    return __myos_syscall6(
        SYSCALL_CHDIR,
        (uint64_t)
            (uintptr_t) path,
        (uint64_t) path_length,
        0,
        0,
        0,
        0
    );
}

syscall_result_t myos_getcwd(
    char *buffer,
    size_t capacity)
{
    return __myos_syscall6(
        SYSCALL_GETCWD,
        (uint64_t)
            (uintptr_t) buffer,
        (uint64_t) capacity,
        0,
        0,
        0,
        0
    );
}

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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file file.c
 * @brief Native bootstrap file-descriptor syscall wrappers.
 */

#include <myos/file.h>

#include "syscall.h"

#include <stdint.h>

syscall_result_t myos_fd_open(
    const char *path,
    size_t path_length,
    uint64_t access)
{
    return __myos_syscall6(
        SYSCALL_FD_OPEN,
        (uint64_t)
            (uintptr_t) path,
        (uint64_t) path_length,
        access,
        0,
        0,
        0
    );
}

syscall_result_t myos_fd_close(
    uint64_t descriptor)
{
    return __myos_syscall6(
        SYSCALL_FD_CLOSE,
        descriptor,
        0,
        0,
        0,
        0,
        0
    );
}

syscall_result_t myos_fd_read(
    uint64_t descriptor,
    void *buffer,
    size_t size)
{
    return __myos_syscall6(
        SYSCALL_FD_READ,
        descriptor,
        (uint64_t)
            (uintptr_t) buffer,
        (uint64_t) size,
        0,
        0,
        0
    );
}

syscall_result_t myos_fd_write(
    uint64_t descriptor,
    const void *buffer,
    size_t size)
{
    return __myos_syscall6(
        SYSCALL_FD_WRITE,
        descriptor,
        (uint64_t)
            (uintptr_t) buffer,
        (uint64_t) size,
        0,
        0,
        0
    );
}

syscall_result_t myos_fd_lseek(
    uint64_t descriptor,
    int64_t offset,
    enum syscall_seek_origin origin)
{
    return __myos_syscall6(
        SYSCALL_FD_LSEEK,
        descriptor,
        (uint64_t) offset,
        (uint64_t) origin,
        0,
        0,
        0
    );
}

syscall_result_t myos_fd_fstat(
    uint64_t descriptor,
    struct syscall_file_stat *status)
{
    return __myos_syscall6(
        SYSCALL_FD_FSTAT,
        descriptor,
        (uint64_t)
            (uintptr_t) status,
        0,
        0,
        0,
        0
    );
}

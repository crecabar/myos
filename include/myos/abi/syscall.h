// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file syscall.h
 * @brief Published MyOS x86-64 system call ABI.
 */

#ifndef MYOS_ABI_SYSCALL_H
#define MYOS_ABI_SYSCALL_H

/*
 * MyOS x86-64 syscall ABI version.
 *
 * ABI version 1 uses:
 *
 *   int 0x80
 *
 *   RAX = syscall number on entry
 *   RDI = argument 0
 *   RSI = argument 1
 *   RDX = argument 2
 *   R10 = argument 3
 *   R8  = argument 4
 *   R9  = argument 5
 *
 * On return:
 *
 *   RAX = syscall result
 *
 * Successful results are non-negative signed 64-bit values.
 * Errors are returned as the negative value of enum syscall_error.
 *
 * Returning syscalls preserve every general-purpose register except RAX.
 * The original user RSP and RFLAGS are restored, and execution resumes after
 * the int 0x80 instruction.
 *
 * Production syscall numbers are append-only once published by an ABI
 * version. Existing numbers must not be reused or assigned a different
 * meaning. Incompatible ABI changes require a version change.
 */
#define SYSCALL_ABI_VERSION 1

#define SYSCALL_DEBUG_PUTC 1
#define SYSCALL_EXIT       2
#define SYSCALL_YIELD      3
#define SYSCALL_WRITE      4
#define SYSCALL_WAITPID    5
#define SYSCALL_FORK       6
#define SYSCALL_FD_OPEN    7
#define SYSCALL_FD_CLOSE   8
#define SYSCALL_FD_READ    9
#define SYSCALL_FD_WRITE   10
#define SYSCALL_FD_LSEEK   11
#define SYSCALL_FD_FSTAT   12
#define SYSCALL_GETPID     13
#define SYSCALL_CHDIR      14
#define SYSCALL_GETCWD     15

#define SYSCALL_WAITPID_NOHANG (1 << 0)

#define SYSCALL_OPEN_ACCESS_READ  (1 << 0)
#define SYSCALL_OPEN_ACCESS_WRITE (1 << 1)

#define SYSCALL_PATH_MAX 4096

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int64_t syscall_result_t;

/*
 * Stable MyOS syscall error identifiers.
 *
 * These numeric values belong to the MyOS syscall ABI. Append new values as
 * required; never renumber or reuse an existing value.
 *
 * libc may translate these identifiers into its public errno representation
 * independently of the kernel ABI.
 */
enum syscall_error {
    SYSCALL_ERROR_INVALID_ARGUMENT = 1,
    SYSCALL_ERROR_BAD_ADDRESS = 2,
    SYSCALL_ERROR_NOT_IMPLEMENTED = 3,
    SYSCALL_ERROR_NO_CHILD = 4,
    SYSCALL_ERROR_RESOURCE_EXHAUSTED = 5,
    SYSCALL_ERROR_BAD_DESCRIPTOR = 6,
    SYSCALL_ERROR_NOT_FOUND = 7,
    SYSCALL_ERROR_NOT_DIRECTORY = 8,
    SYSCALL_ERROR_NOT_SUPPORTED = 9,
    SYSCALL_ERROR_ACCESS_DENIED = 10,
    SYSCALL_ERROR_OVERFLOW = 11
};

_Static_assert(
    sizeof(syscall_result_t) == sizeof(uint64_t),
    "syscall result must occupy one x86-64 register"
);

static inline syscall_result_t syscall_result_error(
    enum syscall_error error)
{
    return
        -(syscall_result_t) error;
}

static inline bool syscall_result_is_error(
    syscall_result_t result)
{
    return result < 0;
}

enum syscall_wait_termination_reason {
    SYSCALL_WAIT_TERMINATION_EXITED = 1,
    SYSCALL_WAIT_TERMINATION_SEGMENTATION_FAULT,
    SYSCALL_WAIT_TERMINATION_ILLEGAL_INSTRUCTION,
    SYSCALL_WAIT_TERMINATION_PROTECTION_FAULT,
    SYSCALL_WAIT_TERMINATION_ARITHMETIC_FAULT,
    SYSCALL_WAIT_TERMINATION_TRAP,
};

struct syscall_wait_status {
    uint64_t termination_reason;
    uint64_t exit_status;
};

_Static_assert(
    sizeof(struct syscall_wait_status) == 16,
    "syscall wait status ABI must occupy 16 bytes"
);

enum syscall_seek_origin {
    SYSCALL_SEEK_ORIGIN_START = 0,
    SYSCALL_SEEK_ORIGIN_CURRENT = 1,
    SYSCALL_SEEK_ORIGIN_END = 2,
};

enum syscall_file_type {
    SYSCALL_FILE_TYPE_REGULAR_FILE = 1,
    SYSCALL_FILE_TYPE_DIRECTORY = 2,
    SYSCALL_FILE_TYPE_CHARACTER_DEVICE = 3,
};

struct syscall_file_stat {
    uint64_t type;
    uint64_t size;
};

_Static_assert(
    sizeof(struct syscall_file_stat) == 16,
    "syscall file stat ABI must occupy 16 bytes"
);

_Static_assert(
    _Alignof(struct syscall_file_stat) == 8,
    "syscall file stat ABI must be 8-byte aligned"
);

_Static_assert(
    offsetof(struct syscall_file_stat, type) == 0,
    "syscall file stat type offset changed"
);

_Static_assert(
    offsetof(struct syscall_file_stat, size) == 8,
    "syscall file stat size offset changed"
);

#endif

#endif

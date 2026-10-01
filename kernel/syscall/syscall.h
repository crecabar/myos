// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file syscall.h
 * @brief MyOS system call dispatch interface.
 */

#ifndef MYOS_SYSCALL_SYSCALL_H
#define MYOS_SYSCALL_SYSCALL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if MYOS_KERNEL_TESTS
#include "test_numbers.h"
#endif

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
 * Returning syscalls preserve every general-purpose register except
 * RAX. The original user RSP and RFLAGS are restored, and execution
 * resumes after the int 0x80 instruction.
 *
 * Production syscall numbers are append-only once published by an ABI
 * version. Existing numbers must not be reused or assigned a different
 * meaning. Incompatible ABI changes require a version change.
 *
 * MYOS_KERNEL_TESTS-only syscalls are explicitly outside this stability
 * guarantee.
 */
#define SYSCALL_ABI_VERSION 1U

typedef int64_t syscall_result_t;

/*
 * Stable MyOS syscall error identifiers.
 *
 * These numeric values belong to the MyOS syscall ABI. Append new
 * values as required; never renumber or reuse an existing value.
 *
 * A future libc may translate these identifiers into its public errno
 * representation independently of the kernel ABI.
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

/**
 * Debug system call that writes one character through kernel diagnostics.
 */
#define SYSCALL_DEBUG_PUTC 1    // RDI = character
#define SYSCALL_EXIT       2    // RDI = status
#define SYSCALL_YIELD      3    // no arguments
#define SYSCALL_WRITE      4    // RDI = buffer, RSI = length
#define SYSCALL_WAITPID    5    // RDI = pid, RSI = status, RDX = options
#define SYSCALL_FORK       6    // no arguments
#define SYSCALL_FD_OPEN    7
#define SYSCALL_FD_CLOSE   8
#define SYSCALL_FD_READ    9
#define SYSCALL_FD_WRITE   10
#define SYSCALL_FD_LSEEK   11
#define SYSCALL_FD_FSTAT   12

#define SYSCALL_WAITPID_NOHANG (1ULL << 0)

#define SYSCALL_OPEN_ACCESS_READ  (1ULL << 0)
#define SYSCALL_OPEN_ACCESS_WRITE (1ULL << 1)

#define SYSCALL_PATH_MAX 4096U

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

/**
 * Describes how the architecture syscall path must complete waitpid().
 *
 * This is a kernel-internal control result and is not part of the published
 * userspace syscall ABI.
 */
enum syscall_waitpid_action {
    SYSCALL_WAITPID_ACTION_RETURN,
    SYSCALL_WAITPID_ACTION_BLOCK,
};

/**
 * Prepares one waitpid operation.
 *
 * Immediate results are written to result and return
 * SYSCALL_WAITPID_ACTION_RETURN.
 *
 * A blocking wait is registered in the process lifecycle and returns
 * SYSCALL_WAITPID_ACTION_BLOCK. The architecture syscall path must then
 * preserve and suspend the current process context.
 *
 * @param child_pid Direct child PID, or zero for any direct child.
 * @param status_address Userspace status destination, or zero to discard it.
 * @param options waitpid option flags.
 * @param result Receives an immediate syscall result.
 *
 * @return Required completion action.
 */
enum syscall_waitpid_action syscall_waitpid_prepare(
    uint64_t child_pid,
    uint64_t status_address,
    uint64_t options,
    syscall_result_t *result
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

/**
 * Dispatches one system call requested by user mode.
 *
 * MyOS uses the x86-64 system call register convention:
 *
 *   RAX = system call number
 *   RDI = argument 0
 *   RSI = argument 1
 *   RDX = argument 2
 *   R10 = argument 3
 *   R8  = argument 4
 *   R9  = argument 5
 *
 * The system call result is returned in RAX.
 *
 * @param number System call number.
 * @param argument0 First system call argument.
 * @param argument1 Second system call argument.
 * @param argument2 Third system call argument.
 * @param argument3 Fourth system call argument.
 * @param argument4 Fifth system call argument.
 * @param argument5 Sixth system call argument.
 *
 * @return System call result returned to user mode.
 */
syscall_result_t syscall_dispatch(
    uint64_t number,
    uint64_t argument0,
    uint64_t argument1,
    uint64_t argument2,
    uint64_t argument3,
    uint64_t argument4,
    uint64_t argument5
);

#endif

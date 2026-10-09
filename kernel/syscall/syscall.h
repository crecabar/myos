// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file syscall.h
 * @brief MyOS kernel system call dispatch interface.
 */

#ifndef MYOS_SYSCALL_SYSCALL_H
#define MYOS_SYSCALL_SYSCALL_H

#include <myos/abi/syscall.h>

#include <stdint.h>

#if MYOS_KERNEL_TESTS
#include "test_numbers.h"
#endif

struct process_exec_candidate;

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

/**
 * Imports and validates one userspace execve request and prepares a complete
 * replacement process image.
 *
 * path is an explicit byte sequence. argv and envp are userspace arrays of
 * 64-bit pointers to NUL-terminated strings; their element counts are supplied
 * explicitly.
 *
 * All userspace pathname, vector and string data is copied into kernel-owned
 * storage before candidate construction. The current process image remains
 * unchanged throughout this operation.
 *
 * On success, candidate owns a fully prepared replacement and zero is
 * returned. The architecture syscall path must then commit candidate and
 * replace the active interrupt frame. A successful execve therefore never
 * returns to the original userspace image.
 *
 * On failure, candidate remains unprepared and a negative syscall error is
 * returned.
 */
syscall_result_t syscall_execve_prepare(
    uint64_t user_path_address,
    uint64_t path_length,
    uint64_t user_argv_address,
    uint64_t argc,
    uint64_t user_envp_address,
    uint64_t envc,
    struct process_exec_candidate *candidate
);

/**
 * Dispatches one system call requested by user mode.
 *
 * The architecture-specific entry path supplies registers according to the
 * published MyOS syscall ABI.
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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file syscall.h
 * @brief Private bootstrap userspace syscall invocation interface.
 */

#ifndef MYOS_USER_RUNTIME_SYSCALL_H
#define MYOS_USER_RUNTIME_SYSCALL_H

#include <myos/abi/syscall.h>

#include <stdint.h>

/**
 * Invokes one raw MyOS system call with up to six arguments.
 *
 * This function is the architecture-facing boundary of the bootstrap
 * userspace runtime. Higher-level runtime and libc code must use this
 * interface instead of embedding int 0x80 directly.
 *
 * Arguments are accepted using the x86-64 System V C calling convention and
 * translated internally to the published MyOS syscall register convention.
 *
 * @param number Published MyOS syscall number.
 * @param argument0 First syscall argument.
 * @param argument1 Second syscall argument.
 * @param argument2 Third syscall argument.
 * @param argument3 Fourth syscall argument.
 * @param argument4 Fifth syscall argument.
 * @param argument5 Sixth syscall argument.
 *
 * @return Raw MyOS syscall result.
 */
syscall_result_t __myos_syscall6(
    uint64_t number,
    uint64_t argument0,
    uint64_t argument1,
    uint64_t argument2,
    uint64_t argument3,
    uint64_t argument4,
    uint64_t argument5
);

#endif

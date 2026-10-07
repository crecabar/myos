// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file exit.c
 * @brief Bootstrap userspace process termination.
 */

#include "syscall.h"

#include <myos/abi/syscall.h>

#include <stdint.h>

/**
 * Terminates the current userspace process.
 *
 * The status value is sign-extended from the C int representation before
 * crossing the raw syscall boundary, matching the process-exit convention
 * previously implemented directly by crt0.
 *
 * A successful exit syscall never returns. If the kernel unexpectedly returns
 * control, execution remains trapped locally rather than continuing through an
 * invalid runtime state.
 *
 * @param status Process exit status.
 */
_Noreturn void _exit(int status)
{
    (void) __myos_syscall6(
        SYSCALL_EXIT,
        (uint64_t) (int64_t) status,
        0,
        0,
        0,
        0,
        0
    );

    for (;;) {
    }
}

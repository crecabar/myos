// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file exec.h
 * @brief Ring-3 exec image-replacement regression fixture.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_EXEC_H
#define MYOS_TESTS_USER_PROCESSES_EXEC_H

#include <stdint.h>

struct interrupt_context;
struct process;

/**
 * Prepares the Ring-3 exec image-replacement regression.
 */
void user_process_exec_test_prepare(void);

/**
 * Handles the MYOS_KERNEL_TESTS-only exec regression syscall.
 *
 * Operation zero exercises a failed replacement and returns an error to the
 * original userspace image. Operation one commits the prepared target image
 * and rewrites the active syscall frame so IRETQ enters its _start.
 *
 * @param context Active Ring-3 syscall frame.
 * @param operation Exec regression operation.
 */
void user_process_exec_test_syscall(
    struct interrupt_context *context,
    uint64_t operation
);

/**
 * Validates and releases the terminated exec regression process.
 *
 * @param process Detached terminated process.
 */
void user_process_exec_test_terminated(
    struct process *process
);

#endif

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file user_processes.h
 * @brief User-process scheduler test fixtures.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_H
#define MYOS_TESTS_USER_PROCESSES_H

#include "../arch/x86_64/interrupts.h"

#include <stdint.h>

/**
 * Prepares the built-in user processes used to exercise the scheduler.
 *
 * The created processes and their address spaces remain alive for the entire
 * kernel lifetime.
 */
void user_process_tests_prepare(void);

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
 * Handles the scheduler context regression completion gate.
 *
 * Workload zero belongs to process A and succeeds only after the regression
 * has observed enough cross-address-space timer preemptions.
 *
 * Workload one belongs to process B and succeeds only after process A has
 * terminated successfully.
 *
 * @param context Active Ring-3 syscall frame.
 * @param workload Scheduler-context workload identifier.
 */
void user_process_scheduler_context_test_syscall(
    struct interrupt_context *context,
    uint64_t workload
);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file scheduler_context.h
 * @brief Ring-3 scheduler context-switch regression fixture.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_SCHEDULER_CONTEXT_H
#define MYOS_TESTS_USER_PROCESSES_SCHEDULER_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

struct interrupt_context;
struct process;

/**
 * Prepares the two Ring-3 processes used by the scheduler context regression.
 *
 * Both processes execute the same ELF in independent address spaces and use
 * distinct argv values to select workloads A and B.
 */
void user_process_scheduler_context_test_prepare(void);

/**
 * Handles the MYOS_KERNEL_TESTS-only scheduler context completion gate.
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

/**
 * Processes termination of one scheduler-context regression process.
 *
 * Process A completion establishes the survivor phase and returns false.
 * Process B completion validates the final regression invariants and returns
 * true.
 *
 * @param process Detached terminated process.
 *
 * @return true when the complete scheduler-context regression has finished;
 *         false when process A completed and process B must continue.
 */
bool user_process_scheduler_context_test_terminated(
    struct process *process
);

#endif

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file scheduler.h
 * @brief Minimal cooperative process scheduler.
 *
 * Provides the first MyOS runnable-process selection mechanism. Processes are
 * executed until they terminate; preemption and resumable contexts are not
 * implemented yet.
 */

#ifndef MYOS_SCHEDULER_SCHEDULER_H
#define MYOS_SCHEDULER_SCHEDULER_H

#include "../process/process.h"
#include "../arch/x86_64/interrupts.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Initializes the scheduler.
 */
void scheduler_init(void);

/**
 * Adds a READY process to the scheduler.
 *
 * @param process Process to make schedulable.
 *
 * @return true when the process was registered; false otherwise.
 */
bool scheduler_add(struct process *process);

/**
 * Returns the process currently selected for execution.
 *
 * @return Running process, or NULL when no process is active.
 */
struct process *scheduler_current(void);

/**
 * Terminates the current process and transfers execution to the next READY
 * process.
 *
 * This function does not return. If no runnable process remains, MyOS enters
 * its idle state.
 *
 * @param reason Process termination reason.
 */
_Noreturn void scheduler_terminate_current(
    enum process_termination_reason reason
);

/**
 * Terminates the current process normally and schedules the next READY process.
 *
 * @param context Current user-mode syscall interrupt frame.
 * @param status Process exit status.
 */
void scheduler_exit_current(
    struct interrupt_context *context,
    uint64_t status
);

/**
 * Starts execution of the next READY process.
 *
 * Activates the process address space and enters user mode at the process
 * initial RIP and RSP. This function does not return.
 */
_Noreturn void scheduler_run(void);

/**
 * Yields the CPU from the currently running user process.
 *
 * The current user context is saved, the process returns to READY state, and
 * the next READY process is selected using cooperative round-robin scheduling.
 *
 * The supplied interrupt context is rewritten so the syscall return path
 * resumes the selected process.
 *
 * @param context User-mode syscall interrupt frame.
 */
void scheduler_yield_current(struct interrupt_context *context);

#endif

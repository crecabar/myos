// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file scheduler.h
 * @brief Preemptive round-robin process scheduler.
 *
 * Manages runnable processes, CPU context preservation, voluntary yields,
 * process termination, and timer-driven preemption. Each running process
 * receives a fixed scheduling quantum before another READY process may be
 * selected.
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
 * Terminates the current process from an interrupt or exception context.
 *
 * The current process is marked terminated and the supplied interrupt frame
 * is rewritten with the saved state of the next READY process. The interrupt
 * return path can then resume that process without restarting it from its
 * initial entry point.
 *
 * If no runnable process remains, MyOS enters its idle state.
 *
 * @param context Active interrupt frame to replace with the next process.
 * @param reason Process termination reason.
 */
void scheduler_terminate_current_from_interrupt(
    struct interrupt_context *context,
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
 * Voluntarily yields the CPU from the currently running user process.
 *
 * The current user context is saved, the process returns to READY state, and
 * the next READY process is selected using round-robin scheduling.
 *
 * The supplied interrupt context is rewritten so the syscall return path
 * resumes the selected process.
 *
 * @param context User-mode syscall interrupt frame.
 */
void scheduler_yield_current(struct interrupt_context *context);

/**
 * Preempts the currently running process.
 *
 * The interrupted user context is saved, the current process returns to the
 * READY state, and the next READY process is selected using round-robin
 * scheduling.
 *
 * The interrupt frame is rewritten so that returning from the timer interrupt
 * resumes the selected process.
 *
 * @param context User-mode CPU context interrupted by the timer.
 */
void scheduler_preempt_current(struct interrupt_context *context);

/**
 * Accounts for one scheduler timer tick.
 *
 * User-mode execution is preempted when the current process exhausts its
 * scheduling quantum.
 *
 * @param context CPU context interrupted by the timer.
 */
void scheduler_tick(struct interrupt_context *context);

#endif

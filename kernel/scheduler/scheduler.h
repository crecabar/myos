// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file scheduler.h
 * @brief Preemptive round-robin process scheduler.
 *
 * Manages runnable processes, CPU context preservation, voluntary yields,
 * process termination, and timer-driven preemption. Each running process
 * receives a fixed scheduling quantum before another READY process may be
 * selected.
 *
 * The scheduler owns its internal registration slots and scheduling state,
 * but it does not own registered process descriptors or any process memory
 * resources. Registered struct process pointers are borrowed references.
 *
 * A registered process descriptor, together with the memory and layout it
 * references, must remain alive until the scheduler registration has been
 * removed during reaping.
 */

#ifndef MYOS_SCHEDULER_SCHEDULER_H
#define MYOS_SCHEDULER_SCHEDULER_H

#include "../process/process.h"
#include "../arch/x86_64/interrupts.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Maximum number of process registrations held by the scheduler.
 */
#define SCHEDULER_MAX_PROCESSES 8

/**
 * Initializes the scheduler.
 */
void scheduler_init(void);

/**
 * Registers a READY process with the scheduler.
 *
 * The scheduler stores a borrowed reference to the process descriptor.
 * Registration does not transfer ownership of the process, its memory, or its
 * layout to the scheduler.
 *
 * The process descriptor and every object it borrows must remain alive while
 * the process remains registered, including after it reaches
 * PROCESS_STATE_TERMINATED.
 *
 * A process may be registered only once. Registration uses one free scheduler
 * slot and fails when all scheduler slots are occupied.
 *
 * @param process Borrowed process descriptor to make schedulable.
 *
 * @return true when the process was registered; false otherwise.
 */
bool scheduler_add(struct process *process);

/**
 * Removes a terminated process registration from the scheduler.
 *
 * The process must already be in PROCESS_STATE_TERMINATED and must not be the
 * currently running process.
 *
 * Successful removal releases only the scheduler's borrowed reference and
 * makes its slot available for reuse. It does not reclaim process memory,
 * layout resources, lifecycle metadata, PID, or termination information.
 *
 * After this function succeeds, the process lifecycle owner may safely reclaim
 * the process resources.
 *
 * @param process Terminated process whose scheduler registration is removed.
 *
 * @return true when the registration was removed; false otherwise.
 */
bool scheduler_unregister_terminated(struct process *process);

/**
 * Returns the process currently selected for execution.
 *
 * The returned pointer is a borrowed reference owned by the process lifecycle
 * layer. The caller must not destroy or release the process through this
 * pointer.
 *
 * @return Borrowed running process, or NULL when no process is active.
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
 * Termination does not unregister, reap, or destroy the process. Its
 * descriptor and associated lifecycle resources remain alive until a later
 * reaping operation removes the scheduler registration and releases those
 * resources.
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
 * The process is marked PROCESS_STATE_TERMINATED and its final exit status is
 * recorded. The scheduler does not unregister, reap, or destroy the process;
 * its descriptor and associated lifecycle resources remain alive for later
 * reaping.
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

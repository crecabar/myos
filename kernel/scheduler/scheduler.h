// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

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
 * references, must remain alive until the scheduler has detached the
 * registration. For process termination, detachment occurs only after the
 * scheduler has switched back to the kernel-owned address space.
 *
 * When no READY process exists, the scheduler enters an interruptible idle
 * state. The READY set is inspected with maskable interrupts disabled, then
 * the CPU enables interrupts and halts atomically. After an interrupt wakes
 * the CPU, the scheduler rechecks the READY set before sleeping again.
 */

#ifndef MYOS_SCHEDULER_SCHEDULER_H
#define MYOS_SCHEDULER_SCHEDULER_H

#include "../process/process.h"
#include "../arch/x86_64/interrupts.h"
#include "../core/execution/kernel_context.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Maximum number of process registrations held by the scheduler.
 */
#define SCHEDULER_MAX_PROCESSES 8

struct cpu_local;

/**
 * Handles a process after termination has been detached from the scheduler.
 *
 * The scheduler invokes the handler only after switching back to the
 * kernel-owned address space and removing its borrowed process registration.
 *
 * The handler may therefore reclaim the terminated process resources and may
 * subsequently reinitialize and register the same lifecycle storage again.
 *
 * @param process Detached terminated process.
 */
typedef void (*scheduler_terminated_handler)(
    struct process *process
);

/**
 * Executes bounded deferred work from the scheduler idle path.
 *
 * Called with interrupts disabled, outside IRQ context.
 * The handler must not block, enable interrupts, or perform
 * its own context switch.
 *
 * Returns true when at least one work item was executed.
 */
typedef bool (*scheduler_idle_work_handler)(void);

/**
 * Initializes the scheduler.
 */
void scheduler_init(void);

/**
 * Registers one prepared kernel worker with the scheduler.
 *
 * The descriptor and its execution stack remain caller-owned.
 * Registration neither transfers ownership nor executes the worker.
 *
 * This bootstrap interface supports one worker. A second distinct
 * registration is rejected without changing the first.
 */
bool scheduler_register_kernel_worker(
    struct kernel_context *context
);

/**
 * Removes a registered kernel worker that is not executing.
 *
 * The caller remains responsible for the worker's lifetime.
 */
bool scheduler_unregister_kernel_worker(
    struct kernel_context *context
);

/**
 * Dispatches the registered kernel worker when it is READY.
 *
 * The caller must be the current CPU's supervisor, outside
 * interrupt context, with maskable interrupts disabled.
 *
 * Returns true when a worker was dispatched, false when no
 * registered worker is eligible or dispatch was rejected.
 */
bool scheduler_dispatch_kernel_worker(
    struct cpu_local *cpu
);

/**
 * Reports whether the registered kernel worker is READY.
 *
 * This is a scheduling decision only. It does not
 * acquire ownership, change state, or execute the worker.
 *
 * Callers must serialize access to scheduler state.
 */
bool scheduler_kernel_worker_ready(void);

/**
 * Reports whether a timer-interrupted userspace execution
 * has an eligible kernel worker.
 *
 * Does not save user context, alter scheduling state,
 * dispatch a worker, or rewrite the interrupt frame.
 *
 * The caller must serialize access to scheduler state.
 */
bool scheduler_kernel_worker_handoff_needed(
    const struct interrupt_context *context
);

/**
 * Copies a Ring 3 interrupt frame into independent storage.
 *
 * The destination must not overlap the source.
 * This function does not change process state, CPU state,
 * address spaces, or the original interrupt frame.
 *
 * Returns false without changing destination when the
 * arguments are invalid or the frame is not from Ring 3.
 */
bool scheduler_capture_user_interrupt(
    const struct interrupt_context *source,
    struct interrupt_context *destination
);

/**
 * A pending transfer request from userspace to a kernel supervisor.
 *
 * The owner is a borrowed process reference. Its lifetime must
 * extend beyond the request until the request is released.
 *
 * The frame is stored by value, independently of the IRQ stack.
 */
struct scheduler_kernel_handoff {
    bool pending;
    struct process *owner;
    struct interrupt_context frame;
};

/**
 * Validated snapshot for a future return to Ring 3.
 *
 * The owner and address_space are borrowed references.
 * Their lifetime must extend until the plan is consumed.
 *
 * Preparing a plan does not retire the pending handoff.
 */
struct scheduler_handoff_resume_plan {
    const struct process *owner;
    const struct paging_address_space *address_space;
    struct interrupt_context frame;
};

/**
 * Validates a pending handoff before restoring userspace.
 *
 * Requires the kernel address space to be active.
 * The selected process must be the RUNNING owner.
 *
 * On success, publishes a snapshot into independent
 * caller-owned storage. On failure, plan is unchanged.
 *
 * Does not change CR3, consume the handoff or execute IRETQ.
 * plan must not overlap handoff storage.
 */
bool scheduler_kernel_handoff_resume_preflight(
    const struct scheduler_kernel_handoff *handoff,
    const struct process *selected,
    const struct paging_address_space *kernel_space,
    uint64_t active_cr3,
    struct scheduler_handoff_resume_plan *plan
);

/**
 * Initializes an empty handoff request.
 *
 * Must not be called on a live pending request.
 */
void scheduler_kernel_handoff_init(
    struct scheduler_kernel_handoff *handoff
);

/**
 * Prepares a handoff for a running userspace process.
 *
 * Rejects duplicate requests, invalid frames and processes
 * that are not RUNNING. Does not modify the scheduler.
 *
 * Caller guarantees the source frame and handoff storage
 * do not overlap.
 */
bool scheduler_kernel_handoff_prepare(
    struct scheduler_kernel_handoff *handoff,
    struct process *owner,
    const struct interrupt_context *frame
);

/**
 * Releases a pending request only for its owner.
 *
 * Does not resume, dispatch or change the process state.
 */
bool scheduler_kernel_handoff_release(
    struct scheduler_kernel_handoff *handoff,
    const struct process *owner
);

/**
 * Completes an owned handoff and extracts its saved user frame.
 *
 * The owner must still be RUNNING and must match the pending
 * request. The destination must not overlap handoff storage.
 *
 * On success, copies the saved frame to independent storage
 * and clears the pending request.
 *
 * On failure, neither the request nor destination is modified.
 *
 * Does not activate an address space or resume userspace.
 * Caller serializes access with interrupts disabled.
 */
bool scheduler_kernel_handoff_complete(
    struct scheduler_kernel_handoff *handoff,
    const struct process *owner,
    struct interrupt_context *resume_frame
);

/**
 * Prepares the scheduler-owned kernel worker handoff.
 *
 * The owner is necessarily current_process.
 *
 * Requires an eligible registered worker, a RUNNING
 * userspace process and a valid Ring 3 interrupt frame.
 *
 * The caller must execute with interrupts disabled.
 * Does not change execution or process state.
 */
bool scheduler_prepare_kernel_worker_handoff(
    const struct interrupt_context *frame
);

/**
 * Completes the scheduler-owned kernel worker handoff.
 *
 * Only the currently selected RUNNING process may complete
 * its pending request.
 *
 * Does not switch CR3 or perform IRETQ.
 * Caller must execute with interrupts disabled.
 */
bool scheduler_finish_kernel_worker_handoff(
    const struct process *owner,
    struct interrupt_context *resume_frame
);

/**
 * Preflights the scheduler-owned pending handoff.
 *
 * Uses current_process, the active CR3 and the MyOS
 * kernel address space.
 *
 * Does not consume the request or change CPU state.
 * Caller must execute with interrupts disabled.
 */
bool scheduler_kernel_worker_resume_preflight(
    struct scheduler_handoff_resume_plan *plan
);

/**
 * Activates the owner's address space for a validated resume plan.
 *
 * Revalidates the pending handoff against the entire supplied
 * plan before changing CR3.
 *
 * Requires Ring 0, interrupts disabled, kernel CR3 active
 * and stable caller-owned plan storage.
 *
 * On success, the owner's CR3 becomes active, but the
 * handoff remains pending. No IRETQ is performed.
 *
 * A rejected validation leaves CR3 and handoff unchanged.
 * The caller must not enable interrupts between successful
 * activation and the eventual resume or explicit rollback.
 */
bool scheduler_kernel_handoff_activate_resume_space(
    const struct scheduler_kernel_handoff *handoff,
    const struct process *selected,
    const struct scheduler_handoff_resume_plan *plan
);

/**
 * Commits a validated handoff resume after CR3 activation.
 *
 * Requires interrupts disabled and the owner's address
 * space active. The plan must still match the pending
 * request exactly.
 *
 * On success, copies the saved frame into independent
 * storage and retires the handoff exactly once.
 *
 * On rejection, destination and handoff remain unchanged.
 *
 * Does not switch CR3 or execute IRETQ.
 * Frame destinations must not overlap plan/handoff storage.
 */
bool scheduler_kernel_handoff_commit_resume(
    struct scheduler_kernel_handoff *handoff,
    const struct process *selected,
    const struct scheduler_handoff_resume_plan *plan,
    struct interrupt_context *resume_frame
);

/**
 * Activates the current handoff owner's address space.
 *
 * Requires an existing scheduler-owned pending request
 * belonging to current_process.
 *
 * Does not consume the request or resume Ring 3.
 */
bool scheduler_activate_kernel_worker_resume_space(
    const struct scheduler_handoff_resume_plan *plan
);

/**
 * Commits the scheduler-owned handoff for current_process.
 *
 * Called after activating the validated owner's CR3,
 * immediately before restoring user execution.
 */
bool scheduler_commit_kernel_worker_resume(
    const struct scheduler_handoff_resume_plan *plan,
    struct interrupt_context *resume_frame
);

/**
 * Completes a supervisor handoff and resumes its Ring 3 owner.
 *
 * Intended as the non-returning completion callback of
 * kernel_supervisor_bsp_enter().
 *
 * Preconditions:
 * - Running on the BSP supervisor stack in Ring 0.
 * - Kernel CR3 active.
 * - Maskable interrupts disabled.
 * - The worker has returned control to the supervisor.
 * - A scheduler-owned handoff remains pending.
 *
 * The callback validates the owner, activates its address
 * space, commits the saved frame and resumes userspace.
 *
 * Never returns, including on failure.
 *
 * The completion argument must be NULL.
 */
_Noreturn void scheduler_kernel_worker_resume_completion(
    bool dispatched,
    void *argument
);

/**
 * Releases the scheduler-owned pending request.
 *
 * Only the request owner can release it.
 * Does not perform a context switch.
 */
bool scheduler_release_kernel_worker_handoff(
    const struct process *owner
);

/**
 * Reports whether the scheduler owns a pending handoff.
 *
 * The caller must serialize access to scheduler state.
 */
bool scheduler_kernel_worker_handoff_pending(void);

/**
 * Registers one borrowed idle-work handler.
 *
 * Passing NULL disables idle work execution.
 * The caller owns the handler's lifetime.
 */
void scheduler_set_idle_work_handler(
    scheduler_idle_work_handler handler
);

/**
 * Sets the handler invoked for safely detached terminated processes.
 *
 * Only one handler is active at a time. Passing NULL disables termination
 * notification.
 *
 * The handler runs after the terminated process is no longer current, the
 * kernel address space is active, and the scheduler registration has been
 * removed.
 *
 * @param handler Terminated-process lifecycle handler, or NULL.
 */
void scheduler_set_terminated_handler(
    scheduler_terminated_handler handler
);

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
 * Rewrites an active interrupt frame from the saved context of the currently
 * running process.
 *
 * This operation does not change scheduler selection or process state. The
 * current process must be RUNNING and its address space must already be
 * active.
 *
 * This is used when an operation such as exec replaces the current userspace
 * image and needs the pending interrupt return to enter the replacement
 * context rather than resume the previous one.
 *
 * @param context Active interrupt/syscall frame to rewrite.
 *
 * @return true when the current process context was loaded; false when the
 *         scheduler or address-space preconditions were not satisfied.
 */
bool scheduler_load_current_context(
    struct interrupt_context *context
);

/**
 * Terminates the current process from an interrupt or exception context.
 *
 * The current process is marked terminated and the supplied interrupt frame
 * is rewritten with the saved state of the next READY process. The interrupt
 * return path can then resume that process without restarting it from its
 * initial entry point.
 *
 * Once termination metadata has been recorded, the scheduler switches to the
 * kernel-owned address space and removes its borrowed registration. If a
 * terminated-process handler is installed, it is then invoked so the lifecycle
 * owner may safely reclaim or recycle the detached process resources.
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
 * recorded.
 *
 * Once termination metadata has been recorded, the scheduler switches to the
 * kernel-owned address space and removes its borrowed registration. If a
 * terminated-process handler is installed, it is then invoked so the lifecycle
 * owner may safely reclaim or recycle the detached process resources.
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
 * initial RIP and RSP.
 *
 * When no READY process exists, the scheduler enters an interruptible idle
 * state. The READY set is inspected with maskable interrupts disabled, then
 * the CPU enables interrupts and halts atomically. After an interrupt wakes
 * the CPU, the scheduler rechecks the READY set before sleeping again.
 *
 * This function does not return.
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
 * Suspends the current user process for a bounded number of timer ticks.
 *
 * A zero-tick request succeeds without changing process state.
 *
 * For a positive duration, the current user context is saved and
 * the process enters PROCESS_STATE_SLEEPING. Another READY process
 * is selected, or the scheduler enters interruptible idle when
 * none is available.
 *
 * The saved process context is prepared to return zero in RAX
 * when the process eventually resumes.
 *
 * This operation is intended to be called from the user-process
 * syscall interrupt path, with maskable interrupts disabled.
 * The caller must not modify the interrupt frame after a
 * successful positive-duration suspension: that frame may
 * already describe a different process.
 *
 * @param context Current user-mode syscall interrupt frame.
 * @param duration_ticks Requested wait in timer ticks.
 *
 * @return true when the request was accepted; false when its
 *         preconditions were not satisfied.
 */
 bool scheduler_sleep_current(
    struct interrupt_context *context,
    uint64_t duration_ticks
);

/**
 * Blocks the current user process until an explicit wakeup.
 *
 * Saves the interrupted user context, marks the process BLOCKED,
 * and selects another READY process. If none is available,
 * the scheduler enters interruptible idle.
 *
 * The saved context returns zero in RAX when the process resumes.
 *
 * This operation must be called from the syscall interrupt path
 * with maskable interrupts disabled. On successful suspension,
 * the supplied interrupt frame may describe another process;
 * the caller must not overwrite it.
 *
 * @param context Current user-mode syscall interrupt frame.
 *
 * @return true when the request was accepted; false otherwise.
 */
bool scheduler_block_current(struct interrupt_context *context);

/**
 * Blocks the current process while preserving the complete interrupted
 * userspace context exactly as supplied.
 *
 * Unlike scheduler_block_current(), this operation does not replace saved RAX
 * with zero. It is intended for restartable operations whose caller has
 * already prepared the context that must execute after wakeup.
 *
 * @param context Current user-mode interrupt frame.
 *
 * @return true when the process was blocked; false otherwise.
 */
bool scheduler_block_current_preserve_context(
    struct interrupt_context *context
);

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

enum scheduler_timer_decision {
    SCHEDULER_TIMER_CONTINUE,
    SCHEDULER_TIMER_PREEMPT,
    SCHEDULER_TIMER_HANDOFF,
};

/**
 * Pure scheduling policy for a timer decision.
 *
 * elapsed_ticks represents the current process quantum
 * after accounting for the incoming timer tick.
 *
 * A pending handoff takes precedence over both worker
 * dispatch and ordinary preemption. CONTINUE means no
 * additional scheduling action, not permission to
 * return to userspace while a handoff is pending.
 *
 * Does not inspect or modify scheduler global state.
 */
enum scheduler_timer_decision scheduler_timer_decide(
    bool user_running,
    bool worker_ready,
    bool handoff_pending,
    uint64_t elapsed_ticks,
    uint64_t quantum_ticks
);

/**
 * Evaluates the timer decision against current scheduler state.
 *
 * The incoming timer tick has not yet been accounted for.
 * Does not modify the quantum, process state, CR3 or frame.
 *
 * Caller must serialize scheduler state with interrupts disabled.
 */
enum scheduler_timer_decision scheduler_timer_decision_for(
    const struct interrupt_context *context
);

/**
 * Accounts for one timer tick interrupted from Ring 3.
 *
 * CONTINUE leaves the current interrupt frame unchanged.
 *
 * PREEMPT uses the ordinary scheduler context-switch path
 * and returns through the timer interrupt frame.
 *
 * HANDOFF captures the interrupted process context and
 * transfers execution to the non-returning BSP supervisor.
 * The supervisor dispatches the worker and explicitly
 * resumes the owning process through IRETQ.
 *
 * The hardware timer EOI must already have been sent
 * before this function is called.
 *
 * An existing pending handoff is a fatal invariant
 * violation on this entry path.
 *
 * Caller executes on the BSP with interrupts disabled.
 */
void scheduler_tick(struct interrupt_context *context);

/**
 * Makes registered sleeping processes runnable when their timer
 * wait has elapsed.
 *
 * Called once per timer tick. This operation updates process
 * states but does not select a process or switch CPU context.
 *
 * BLOCKED processes do not have timer-driven wakeup semantics.
 *
 * @param now_ticks Current monotonically advancing timer count.
 */
void scheduler_wake_sleepers(uint64_t now_ticks);

/**
 * Explicitly wakes a registered BLOCKED process.
 *
 * The caller must hold the scheduler's single-CPU critical
 * section by entering with maskable interrupts disabled.
 *
 * A successful wakeup changes BLOCKED to READY without
 * immediately selecting or executing the process.
 *
 * Processes in any other state, unregistered processes,
 * and repeated wakeup requests are rejected.
 *
 * @param process Borrowed descriptor of the process to wake.
 *
 * @return true when the registered process was awakened;
 *         false when the request was not applicable.
 */
bool scheduler_wake_blocked(struct process *process);

#if MYOS_KERNEL_TESTS
/**
 * Returns the number of process descriptors currently registered with the
 * scheduler.
 *
 * @return Number of occupied scheduler registration slots.
 */
size_t scheduler_test_process_count(void);

/**
 * Returns the number of timer-driven process preemptions observed since the
 * scheduler was initialized.
 *
 * @return Timer-driven preemption count.
 */
uint64_t scheduler_test_preemption_count(void);

/**
 * Returns the number of timer-driven preemptions whose successor belonged to
 * a different process address space.
 *
 * @return Cross-address-space preemption count.
 */
uint64_t scheduler_test_cross_address_space_preemption_count(void);

/**
 * Exercises the idle-work dispatch path without entering HLT.
 *
 * Test code must establish the interrupt state required
 * by the registered callback.
 */
bool scheduler_test_dispatch_idle_work(void);

/**
 * Exercises the production idle kernel-worker dispatch
 * without entering the non-returning idle loop.
 *
 * Requires interrupts disabled and kernel CR3 active.
 *
 * Returns false when no worker is eligible.
 */
bool scheduler_test_dispatch_idle_kernel_worker(void);
#endif

#endif

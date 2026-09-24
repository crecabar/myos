// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process.h
 * @brief Process execution state and lifecycle tracking.
 *
 * Process lifecycle ownership is separate from scheduler registration.
 * A lifecycle owner keeps the process descriptor, process memory, and process
 * layout alive while the process is registered with the scheduler.
 *
 * The scheduler borrows the process descriptor and may transition its
 * execution state, but never owns or destroys lifecycle resources.
 *
 * When a process terminates through the scheduler, the scheduler first
 * records its final termination state, switches back to the kernel-owned
 * address space, and removes its borrowed registration.
 *
 * Once detached, the process lifecycle owner may release layout-managed
 * mappings, destroy the empty process address space, and finally release or
 * recycle lifecycle metadata.
 */

#ifndef MYOS_PROCESS_PROCESS_H
#define MYOS_PROCESS_PROCESS_H

#include "layout.h"
#include "memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Represents the execution state of a process.
 *
 * READY processes may be selected by the scheduler.
 * RUNNING identifies the process currently executing.
 *
 * BLOCKED processes wait for an explicit event-driven wakeup.
 * SLEEPING processes wait for a timer-driven wakeup.
 *
 * Neither BLOCKED nor SLEEPING processes are runnable.
 * TERMINATED processes cannot transition back to a runnable state.
 *
 * Execution-state transitions are managed by the scheduler.
 */
enum process_state {
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_SLEEPING,
    PROCESS_STATE_TERMINATED,
};

/**
 * Describes why a process stopped executing.
 */
enum process_termination_reason {
    PROCESS_TERMINATION_NONE,
    PROCESS_TERMINATION_EXITED,
    PROCESS_TERMINATION_SEGMENTATION_FAULT,
    PROCESS_TERMINATION_ILLEGAL_INSTRUCTION,
    PROCESS_TERMINATION_PROTECTION_FAULT,
    PROCESS_TERMINATION_ARITHMETIC_FAULT,
    PROCESS_TERMINATION_TRAP,
};

/**
 * Represents the resumable user-mode CPU state of a process.
 *
 * General-purpose registers together with RIP, RSP, and RFLAGS are saved when
 * a process yields the CPU and restored when the scheduler resumes it.
 */
struct process_context {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t rip;
    uint64_t rsp;
    uint64_t rflags;
};

_Static_assert(
    sizeof(struct process_context) == 144,
    "struct process_context must be 144 bytes"
);

_Static_assert(
    _Alignof(struct process_context) == 8,
    "struct process_context must be 8-byte aligned"
);

_Static_assert(offsetof(struct process_context, r15) == 0, "process_context.r15 offset mismatch");
_Static_assert(offsetof(struct process_context, r14) == 8, "process_context.r14 offset mismatch");
_Static_assert(offsetof(struct process_context, r13) == 16, "process_context.r13 offset mismatch");
_Static_assert(offsetof(struct process_context, r12) == 24, "process_context.r12 offset mismatch");
_Static_assert(offsetof(struct process_context, r11) == 32, "process_context.r11 offset mismatch");
_Static_assert(offsetof(struct process_context, r10) == 40, "process_context.r10 offset mismatch");
_Static_assert(offsetof(struct process_context, r9) == 48, "process_context.r9 offset mismatch");
_Static_assert(offsetof(struct process_context, r8) == 56, "process_context.r8 offset mismatch");

_Static_assert(offsetof(struct process_context, rbp) == 64, "process_context.rbp offset mismatch");
_Static_assert(offsetof(struct process_context, rdi) == 72, "process_context.rdi offset mismatch");
_Static_assert(offsetof(struct process_context, rsi) == 80, "process_context.rsi offset mismatch");
_Static_assert(offsetof(struct process_context, rdx) == 88, "process_context.rdx offset mismatch");
_Static_assert(offsetof(struct process_context, rcx) == 96, "process_context.rcx offset mismatch");
_Static_assert(offsetof(struct process_context, rbx) == 104, "process_context.rbx offset mismatch");
_Static_assert(offsetof(struct process_context, rax) == 112, "process_context.rax offset mismatch");

_Static_assert(offsetof(struct process_context, rip) == 120, "process_context.rip offset mismatch");
_Static_assert(offsetof(struct process_context, rsp) == 128, "process_context.rsp offset mismatch");
_Static_assert(offsetof(struct process_context, rflags) == 136, "process_context.rflags offset mismatch");

/**
 * Represents a schedulable MyOS user process.
 *
 * The process descriptor owns its execution-state fields and saved CPU
 * context, but it does not own the process_memory or process_layout objects
 * referenced by memory and layout.
 *
 * Those objects are borrowed from the process lifecycle owner and must remain
 * alive for at least as long as this descriptor may be referenced by the
 * scheduler.
 *
 * PROCESS_STATE_TERMINATED means that the process will never execute again.
 * It does not by itself imply that lifecycle resources have been reclaimed.
 *
 * Processes terminated through the scheduler are detached from their
 * scheduler registration before being handed back to the lifecycle owner.
 * Their descriptor, layout, memory, PID, and termination information remain
 * valid until the lifecycle owner explicitly reclaims or recycles them.
 */
 struct process {
    uint64_t id;
    enum process_state state;
    enum process_termination_reason termination_reason;
    uint64_t exit_status;

    /*
     * Timer-wait metadata owned by the scheduler.
     *
     * These fields are meaningful only while the process is
     * SLEEPING. The requested duration must be greater than zero
     * and must not exceed TIMER_SLEEP_MAX_TICKS.
     *
     * The scheduler will determine expiration using unsigned
     * elapsed-tick arithmetic:
     *
     *     (uint64_t) (now - sleep_start_ticks)
     *         >= sleep_duration_ticks
     *
     * Both fields are zero for a newly initialized process.
     * They must be cleared when its timed wait completes.
     */
    uint64_t sleep_start_ticks;
    uint64_t sleep_duration_ticks;

    struct process_memory *memory;
    struct process_layout *layout;
    struct process_context context;
};

/**
 * Initializes a schedulable process descriptor.
 *
 * The process borrows the supplied memory and layout objects. Ownership of
 * those objects remains with the caller/lifecycle owner, which must keep them
 * alive while the initialized process may still be referenced.
 *
 * This function allocates no resources and takes no ownership. On failure,
 * the caller remains responsible for all supplied objects.
 *
 * @param process Process descriptor to initialize.
 * @param id Process identifier.
 * @param memory Borrowed process address space.
 * @param layout Borrowed user virtual-memory layout and initial execution
 *        addresses.
 *
 * @return true when the descriptor was initialized; false otherwise.
 */
bool process_init(
    struct process *process,
    uint64_t id,
    struct process_memory *memory,
    struct process_layout *layout
);

/**
 * Reclaims resources associated with a terminated process.
 *
 * The caller must be the process lifecycle owner and must ensure that the
 * process is no longer referenced by the scheduler before calling this
 * function.
 *
 * The process must already be in PROCESS_STATE_TERMINATED. Its layout-managed
 * mappings are released first, followed by its now-empty process address
 * space.
 *
 * This function does not release or recycle the process descriptor, process
 * memory metadata object, process layout metadata object, PID, termination
 * information, or scheduler capacity.
 *
 * On success, the process no longer references its former memory or layout.
 *
 * A teardown failure indicates an inconsistent lifecycle or memory state.
 * Resources already released before the failure are not reconstructed.
 *
 * @param process Terminated process whose associated resources are reclaimed.
 *
 * @return true when all associated resources were reclaimed; false when the
 *         process is invalid, is not terminated, or teardown fails.
 */
bool process_reclaim_resources(struct process *process);

#endif

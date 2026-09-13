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
 * A terminated process remains registered and retains its final termination
 * information until reaping. Reaping must first remove scheduler references,
 * then release layout-managed mappings, destroy the empty process address
 * space, and finally release or recycle lifecycle metadata.
 */

#ifndef MYOS_PROCESS_PROCESS_H
#define MYOS_PROCESS_PROCESS_H

#include "layout.h"
#include "memory.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Represents the execution state of a process.
 */
enum process_state {
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
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
 * PROCESS_STATE_TERMINATED means that the process will never execute again,
 * but does not imply that its descriptor, layout, memory, or scheduler slot
 * have been reclaimed. Resource destruction happens later during reaping.
 */
struct process {
    uint64_t id;
    enum process_state state;
    enum process_termination_reason termination_reason;
    uint64_t exit_status;

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

#endif

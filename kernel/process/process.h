// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process.h
 * @brief Process execution state and lifecycle tracking.
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
 * The process descriptor tracks execution state and references the virtual
 * memory and layout required to enter its user-mode execution environment.
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
 * @param process Process descriptor to initialize.
 * @param id Process identifier.
 * @param memory Process address space.
 * @param layout User virtual-memory layout and initial execution addresses.
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

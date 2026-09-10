// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file stack.h
 * @brief User stack layout and lifecycle management.
 */

#ifndef MYOS_PROCESS_STACK_H
#define MYOS_PROCESS_STACK_H

#include "memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Represents a user-mode stack owned by a process.
 *
 * The stack consists of one unmapped guard page followed by a contiguous
 * range of writable user pages. The stack grows downward from stack_top.
 */
struct process_stack {
    uint64_t guard_address;
    uint64_t base_address;
    uint64_t stack_top;
    size_t page_count;
};

/**
 * Creates a user stack inside a process address space.
 *
 * The guard page remains deliberately unmapped. The stack pages are allocated
 * immediately above it and are mapped writable and user-accessible.
 *
 * @param memory Process memory that will own the stack pages.
 * @param stack Stack descriptor to initialize.
 * @param stack_top Virtual address immediately above the stack.
 * @param page_count Number of mapped 4 KiB stack pages.
 *
 * @return true when the complete stack was created; false otherwise.
 */
bool process_stack_create(
    struct process_memory *memory,
    struct process_stack *stack,
    uint64_t stack_top,
    size_t page_count
);

/**
 * Releases all mapped pages belonging to a user stack.
 *
 * The guard page remains unmapped throughout the stack lifetime and therefore
 * owns no physical frame.
 *
 * @param memory Process memory that owns the stack.
 * @param stack Stack descriptor to destroy.
 *
 * @return true when all stack pages were released; false otherwise.
 */
bool process_stack_destroy(
    struct process_memory *memory,
    struct process_stack *stack
);

#endif

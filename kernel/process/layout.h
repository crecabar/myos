// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file layout.h
 * @brief Minimal user process virtual memory layout.
 *
 * Defines the initial virtual memory organization required by a MyOS process:
 * executable user code and a protected user stack.
 */

#ifndef MYOS_PROCESS_LAYOUT_H
#define MYOS_PROCESS_LAYOUT_H

#include "memory.h"
#include "stack.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Default virtual address where process code begins.
 */
#define PROCESS_LAYOUT_CODE_BASE 0x0000000000400000ULL

/**
 * Default initial user stack top.
 */
#define PROCESS_LAYOUT_STACK_TOP 0x0000000070000000ULL

/**
 * Number of mapped pages in the initial user stack.
 */
#define PROCESS_LAYOUT_STACK_PAGES 4

/**
 * Represents the minimal virtual address-space layout of a process.
 *
 * The layout owns an executable code page and a guarded writable user stack
 * inside an existing process memory address space.
 */
struct process_layout {
    uint64_t code_base;
    uint64_t entry_point;
    struct process_stack stack;
};

/**
 * Creates the minimal virtual memory layout for a process.
 *
 * One executable, read-only user code page is created at the default code
 * address. A writable, non-executable user stack with one unmapped guard page
 * is created near the top of the lower address space.
 *
 * @param memory Process memory that will own the layout.
 * @param layout Layout descriptor to initialize.
 *
 * @return true when the complete layout was created; false otherwise.
 */
bool process_layout_create(
    struct process_memory *memory,
    struct process_layout *layout
);

/**
 * Releases all memory regions owned by a process layout.
 *
 * The process memory object itself is not destroyed by this function.
 *
 * @param memory Process memory that owns the layout.
 * @param layout Layout descriptor to destroy.
 *
 * @return true when all layout-owned mappings were released; false otherwise.
 */
bool process_layout_destroy(
    struct process_memory *memory,
    struct process_layout *layout
);

#endif

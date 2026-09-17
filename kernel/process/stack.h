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
 * Represents a user-mode stack layout inside process memory.
 *
 * The stack descriptor tracks and manages the lifetime of the mapped user
 * pages created for the stack, but does not own the process_memory object
 * containing those mappings.
 *
 * The guard page remains deliberately unmapped and therefore owns no physical
 * frame.
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
 * The supplied process memory is borrowed. On successful creation, the stack
 * descriptor becomes responsible for releasing the newly allocated stack
 * mappings through process_stack_destroy().
 *
 * If creation fails, the descriptor manages no remaining stack mappings.
 *
 * @param memory Borrowed process memory in which stack pages are created.
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
 * Builds the initial userspace execution stack.
 *
 * The initial MyOS userspace stack contains, in ascending virtual-address
 * order, argc, argv pointers, a null argv terminator, envp pointers, and a
 * null envp terminator. Argument and environment strings are copied into the
 * upper portion of the mapped stack.
 *
 * The returned initial stack pointer is 16-byte aligned.
 *
 * This initial ABI deliberately does not provide an auxiliary vector yet.
 *
 * The stack mappings must already have been created through
 * process_stack_create(). Their mappings and permissions are not changed.
 *
 * A zero argument or environment count permits the corresponding pointer
 * array to be NULL. Every string referenced by a non-empty array must be
 * non-NULL and null-terminated.
 *
 * @param memory Borrowed process memory containing the stack mappings.
 * @param stack Existing mapped user stack.
 * @param argc Number of argument strings.
 * @param argv Kernel-accessible argument string pointers.
 * @param envc Number of environment strings.
 * @param envp Kernel-accessible environment string pointers.
 * @param initial_rsp Receives the initial aligned user stack pointer.
 *
 * @return true when the complete initial stack was built; false otherwise.
 */
bool process_stack_build_initial(
    struct process_memory *memory,
    const struct process_stack *stack,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[],
    uint64_t *initial_rsp
);

/**
 * Releases all mapped pages managed by a user stack.
 *
 * The supplied process memory is borrowed and remains alive after this
 * operation. The guard page is unmapped throughout the stack lifetime and
 * therefore owns no physical frame.
 *
 * On success, the stack descriptor is responsible for no remaining
 * process-memory mappings.
 *
 * @param memory Borrowed process memory containing the stack mappings.
 * @param stack Stack descriptor whose managed mappings are to be released.
 *
 * @return true when all stack pages managed by the descriptor were released; false otherwise.
 */
bool process_stack_destroy(
    struct process_memory *memory,
    struct process_stack *stack
);

#endif

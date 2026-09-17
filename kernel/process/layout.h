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

#include "../elf/elf64_loader.h"
#include "memory.h"
#include "stack.h"

#include <stdbool.h>
#include <stddef.h>
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
 * Identifies the mapping ownership model used by a process layout.
 */
enum process_layout_kind {
    PROCESS_LAYOUT_KIND_NONE = 0,
    PROCESS_LAYOUT_KIND_LEGACY,
    PROCESS_LAYOUT_KIND_ELF64,
};

/**
 * Represents the minimal virtual address-space layout of a process.
 *
 * The layout defines and manages the lifetime of the user mappings created for
 * its executable code and stack inside a borrowed process_memory address
 * space. It also records the initial user instruction and stack pointers used
 * when the process begins execution.
 *
 * The layout does not own the process_memory object itself. That memory object
 * must remain alive until all mappings managed by the layout have been
 * released through process_layout_destroy().
 */
 struct process_layout {
    enum process_layout_kind kind;

    uint64_t code_base;
    uint64_t entry_point;
    uint64_t initial_rsp;

    struct process_stack stack;
    struct elf64_loaded_image loaded_image;
};

/**
 * Creates the minimal virtual memory layout for a process.
 *
 * One executable, read-only user code page is created at the default code
 * address. A writable, non-executable user stack with one unmapped guard page
 * is created near the top of the lower address space.
 *
 * The supplied process memory is borrowed. On successful creation, the layout
 * assumes responsibility for releasing the newly created code and stack
 * mappings, but does not take ownership of the process_memory object itself.
 *
 * If creation fails, this function rolls back any mappings created for the
 * layout before the failure and the caller retains ownership of both input
 * objects.
 *
 * @param memory Borrowed process memory in which layout mappings are created.
 * @param layout Layout descriptor to initialize.
 *
 * @return true when the complete layout was created; false otherwise.
 */
bool process_layout_create(
    struct process_memory *memory,
    struct process_layout *layout
);

/**
 * Creates a process virtual memory layout from an ELF64 executable image.
 *
 * ELF64 PT_LOAD mappings are loaded into the borrowed process address space.
 * A writable, non-executable user stack is then created and initialized with
 * the minimal MyOS process-entry ABI containing argc, argv and envp.
 *
 * The ELF entry point must belong to the semantic range of an executable
 * PT_LOAD segment.
 *
 * On success, the layout owns both the loaded ELF mappings and the user stack.
 * The original ELF image buffer remains borrowed and need not remain alive
 * after this function returns.
 *
 * On failure, every mapping and metadata resource created by this operation is
 * released before returning.
 *
 * @param memory Borrowed process memory in which mappings are created.
 * @param image Parsed ELF64 executable image.
 * @param argc Number of argv strings.
 * @param argv Argument strings, or NULL when argc is zero.
 * @param envc Number of environment strings.
 * @param envp Environment strings, or NULL when envc is zero.
 * @param layout Layout descriptor to initialize.
 *
 * @return true when the complete ELF64 process layout was created; false
 * otherwise.
 */
bool process_layout_create_elf64(
    struct process_memory *memory,
    const struct elf64_image *image,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[],
    struct process_layout *layout
);

/**
 * Releases all mappings managed by a process layout.
 *
 * The supplied process memory is borrowed and is not destroyed by this
 * function.
 *
 * On success, the layout is responsible for no remaining process-memory
 * mappings.
 *
 * @param memory Borrowed process memory containing the layout mappings.
 * @param layout Layout whose managed mappings are to be released.
 *
 * @return true when all layout-managed mappings were released; false otherwise.
 */
bool process_layout_destroy(
    struct process_memory *memory,
    struct process_layout *layout
);

#endif

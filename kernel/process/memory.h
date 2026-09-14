// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file memory.h
 * @brief Process virtual memory ownership and lifecycle management.
 *
 * Defines the process-facing memory API built on top of the x86-64 paging
 * subsystem. Process memory owns its private user mappings while inheriting
 * the shared kernel mappings required to execute kernel code in every address
 * space.
 */

#ifndef MYOS_PROCESS_MEMORY_H
#define MYOS_PROCESS_MEMORY_H

#include "../arch/x86_64/paging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Lowest virtual address permitted for process userspace.
 *
 * The first 4 KiB virtual page is intentionally excluded so null and
 * near-null pointers cannot refer to legal process memory.
 */
#define PROCESS_USER_VIRTUAL_MIN 0x0000000000001000ULL

/**
 * Highest virtual address permitted for process userspace.
 *
 * This is the final address in the canonical lower half of the current
 * four-level x86-64 virtual address space.
 */
#define PROCESS_USER_VIRTUAL_MAX 0x00007FFFFFFFFFFFULL

/**
 * Represents the virtual memory owned by a process lifecycle.
 *
 * Each process memory instance owns its PML4 root, private lower-half page
 * tables, and physical frames allocated through its allocate APIs until those
 * frames are explicitly released.
 *
 * Frames supplied to process_memory_map_page() are borrowed and do not become
 * owned by process memory merely because they are mapped.
 *
 * Kernel mappings in the upper half are borrowed from the kernel address
 * space. They are shared, must not be mutated through this object, and are
 * never reclaimed when process memory is destroyed.
 */
struct process_memory {
    struct paging_address_space address_space;
};

/**
 * Checks whether a complete virtual-address range belongs to process
 * userspace.
 *
 * The start address must lie between PROCESS_USER_VIRTUAL_MIN and
 * PROCESS_USER_VIRTUAL_MAX, inclusive. For a non-empty range, the final byte
 * must also remain inside those bounds and address arithmetic must not
 * overflow.
 *
 * A zero-length range is valid when its start address itself is a legal
 * userspace address.
 *
 * @param virtual_address First virtual address in the range.
 * @param size Number of bytes in the range.
 *
 * @return true when the complete range is legal process userspace; false
 *         otherwise.
 */
bool process_memory_user_range_valid(
    uint64_t virtual_address,
    size_t size
);

/**
 * Initializes an empty process address space.
 *
 * Creates a new PML4 root with an empty private lower half and inherited
 * kernel mappings in the upper half.
 *
 * @param memory Process memory structure to initialize.
 *
 * @return true when the address space was created successfully; false
 *         otherwise.
 */
bool process_memory_create(struct process_memory *memory);

/**
 * Destroys an empty process address space.
 *
 * The process private address space must already contain no live mappings.
 * Shared kernel mappings are left untouched and only the process-owned PML4
 * root is released.
 *
 * This operation is only valid after all private user mappings have already
 * been released by their respective owners. It does not perform process
 * reaping or implicit layout teardown.
 *
 * @param memory Process memory structure to destroy.
 *
 * @return true when the address space was destroyed successfully; false
 *         otherwise.
 */
bool process_memory_destroy(struct process_memory *memory);

/**
 * Maps an existing physical frame into process user space.
 *
 * The mapping is always marked user-accessible. Missing intermediate page
 * tables are created automatically by the paging subsystem.
 *
 * This operation does not take ownership of or allocate the physical frame.
 *
 * The caller retains ownership of the supplied physical frame. The mapping
 * must be removed with process_memory_unmap_page() before that frame is
 * released or reused by its owner.
 *
 * A frame mapped through this function must not later be released through
 * process_memory_release_page() merely because it is present in this address
 * space.
 *
 * @param memory Process memory to modify.
 * @param virtual_address 4 KiB-aligned user virtual address.
 * @param physical_address 4 KiB-aligned physical frame address.
 * @param writable Whether the resulting mapping is writable.
 *
 * @return true when the mapping was created successfully; false otherwise.
 */
bool process_memory_map_page(
    struct process_memory *memory,
    uint64_t virtual_address,
    uint64_t physical_address,
    bool writable
);

/**
 * Removes a user page mapping without releasing its physical frame.
 *
 * The physical address previously associated with the virtual page is
 * returned to the caller. Empty private page-table levels are reclaimed
 * automatically by the paging subsystem.
 *
 * This operation is only valid for mappings whose backing physical frame is
 * owned by process memory, such as pages created through the process-memory
 * allocation APIs. Caller-owned frames mapped with process_memory_map_page()
 * must instead be removed with process_memory_unmap_page().
 *
 * @param memory Process memory to modify.
 * @param virtual_address 4 KiB-aligned user virtual address to unmap.
 * @param physical_address Receives the physical frame previously mapped.
 *
 * @return true when the mapping existed and was removed; false otherwise.
 */
bool process_memory_unmap_page(
    struct process_memory *memory,
    uint64_t virtual_address,
    uint64_t *physical_address
);

/**
 * Allocates and maps one process-owned user page.
 *
 * A physical frame is obtained from the physical memory manager and mapped at
 * the requested virtual address. If mapping fails, the allocated frame is
 * returned to the physical memory manager before this function returns.
 *
 * @param memory Process memory to modify.
 * @param virtual_address 4 KiB-aligned user virtual address.
 * @param writable Whether the resulting mapping is writable.
 *
 * @return true when the page was allocated and mapped successfully; false
 *         otherwise.
 */
bool process_memory_allocate_page(
    struct process_memory *memory,
    uint64_t virtual_address,
    bool writable
);

/**
 * Releases one process-owned user page.
 *
 * Removes the mapping at the requested virtual address and returns the
 * associated physical frame to the physical memory manager.
 *
 * @param memory Process memory to modify.
 * @param virtual_address 4 KiB-aligned user virtual address to release.
 *
 * @return true when the page was unmapped and its physical frame released;
 *         false otherwise.
 */
bool process_memory_release_page(
    struct process_memory *memory,
    uint64_t virtual_address
);

/**
 * Allocates and maps a contiguous range of user pages.
 *
 * Each virtual page receives its own physical frame. If any allocation or
 * mapping fails, pages already created by this call are released before the
 * function returns.
 *
 * Every mapping in the range must be backed by a physical frame owned by
 * process memory. This function must not be used to release ranges containing
 * caller-owned frames installed through process_memory_map_page().
 *
 * @param memory Process memory to modify.
 * @param virtual_address 4 KiB-aligned start address of the range.
 * @param page_count Number of 4 KiB pages to allocate.
 * @param writable Whether the pages are writable.
 *
 * @return true when the complete range was allocated; false otherwise.
 */
bool process_memory_allocate_pages(
    struct process_memory *memory,
    uint64_t virtual_address,
    size_t page_count,
    bool writable
);

/**
 * Releases a contiguous range of user pages previously owned by the process.
 *
 * Each mapping is removed and its physical frame returned to the physical
 * memory manager.
 *
 * @param memory Process memory to modify.
 * @param virtual_address 4 KiB-aligned start address of the range.
 * @param page_count Number of 4 KiB pages to release.
 *
 * @return true when the complete range was released; false otherwise.
 */
bool process_memory_release_pages(
    struct process_memory *memory,
    uint64_t virtual_address,
    size_t page_count
);

/**
 * Copies bytes into mapped process memory.
 *
 * The destination is resolved through the process address space and written
 * through the kernel direct map. Process page permissions are not modified,
 * allowing the kernel to initialize read-only or executable user mappings.
 *
 * The destination range must already be completely mapped.
 *
 * @param memory Process memory containing the destination mapping.
 * @param virtual_address First process virtual address to write.
 * @param source Source bytes in kernel-accessible memory.
 * @param size Number of bytes to copy.
 *
 * @return true when the complete range was written; false otherwise.
 */
bool process_memory_write(
    struct process_memory *memory,
    uint64_t virtual_address,
    const void *source,
    size_t size
);

/**
 * Copies bytes from a process virtual address into a kernel buffer.
 *
 * The complete source range must be mapped in the process address space and
 * accessible from user mode. The operation fails if any page in the range is
 * unmapped or belongs exclusively to the kernel.
 *
 * @param memory Process address space to read from.
 * @param virtual_address User virtual address of the first byte.
 * @param destination Kernel buffer that receives the copied bytes.
 * @param size Number of bytes to copy.
 *
 * @return true when the complete range was copied; false otherwise.
 */
bool process_memory_read(
    const struct process_memory *memory,
    uint64_t virtual_address,
    void *destination,
    size_t size
);

/**
 * Allocates and maps one executable user page owned by the process.
 *
 * The physical frame is mapped user-accessible and executable, but not
 * writable from the process address space.
 *
 * @param memory Process memory to modify.
 * @param virtual_address 4 KiB-aligned user virtual address.
 *
 * @return true when the executable page was allocated and mapped
 *         successfully; false otherwise.
 */
bool process_memory_allocate_executable_page(
    struct process_memory *memory,
    uint64_t virtual_address
);

#endif

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file heap.h
 * @brief Minimal dynamic kernel heap allocator.
 *
 * Provides dynamic kernel allocations backed by physical frames accessible
 * through the kernel direct map.
 */

#ifndef MYOS_MEMORY_HEAP_H
#define MYOS_MEMORY_HEAP_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Describes the current state of the kernel heap.
 *
 * Statistics are calculated by walking the current heap page and block lists.
 * They describe allocator-visible payload bytes and do not include page or
 * block metadata overhead.
 */
struct kernel_heap_stats {
    size_t page_count;
    size_t allocated_block_count;
    size_t free_block_count;
    size_t allocated_bytes;
    size_t free_bytes;
};

/**
 * Initializes the kernel heap.
 *
 * The heap begins with one physical frame obtained from the physical memory
 * allocator. Additional pages are allocated lazily by kmalloc() when no
 * existing free block can satisfy an allocation.
 *
 * Initialization may only occur once.
 *
 * @return true when the initial heap page was created successfully; false if
 *         the heap is already initialized or no backing frame is available.
 */
bool kernel_heap_init(void);

/**
 * Allocates dynamic kernel memory.
 *
 * Allocations are aligned to 16 bytes and remain entirely within one heap
 * backing page. Requests that cannot fit within the usable payload of one
 * heap page are rejected.
 *
 * Free blocks are reused using first-fit allocation. Blocks may be split when
 * the unused remainder can represent another allocation block.
 *
 * @param size Number of bytes to allocate.
 *
 * @return Pointer to aligned kernel memory, or NULL when size is zero, the
 *         request is too large, or another heap page cannot be allocated.
 */
void *kmalloc(size_t size);

/**
 * Releases a kernel heap allocation.
 *
 * A NULL pointer is ignored. The pointer must otherwise refer exactly to a
 * live allocation previously returned by kmalloc().
 *
 * Adjacent free blocks within the same heap page are coalesced.
 *
 * Heap backing pages are not yet returned to the physical allocator.
 *
 * Invalid pointers and double frees are treated as kernel programming errors.
 *
 * @param pointer Allocation returned by kmalloc(), or NULL.
 */
void kfree(void *pointer);

/**
 * Collects current kernel heap statistics.
 *
 * The heap is walked without modifying allocator state.
 *
 * @param stats Receives the current heap statistics.
 *
 * @return true when statistics were collected successfully; false when the
 *         heap is not initialized, stats is NULL, or accounting overflows.
 */
bool kernel_heap_stats_get(struct kernel_heap_stats *stats);

#endif

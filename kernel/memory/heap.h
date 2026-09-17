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
 * Initializes the kernel heap.
 *
 * The heap begins with one physical frame obtained from the physical memory
 * allocator. Additional pages may be allocated lazily by kmalloc() when the
 * current page no longer has enough space.
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
 * The initial allocator is monotonic: allocated memory is not yet reusable.
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

#endif

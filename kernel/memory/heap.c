// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file heap.c
 * @brief Minimal page-backed dynamic kernel heap allocator.
 */

#include "heap.h"

#include "../core/panic.h"
#include "memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_HEAP_ALIGNMENT 16ULL

struct kernel_heap_block {
    struct kernel_heap_block *previous;
    struct kernel_heap_block *next;
    size_t size;
    bool free;
};

struct kernel_heap_page {
    struct kernel_heap_page *next;
    uint64_t physical_address;
    struct kernel_heap_block *first_block;
};

static struct kernel_heap_page *kernel_heap_first_page;
static struct kernel_heap_page *kernel_heap_last_page;
static bool kernel_heap_initialized;

static bool kernel_heap_align_up(
    size_t value,
    size_t alignment,
    size_t *result
);

static size_t kernel_heap_page_payload_offset(void);

static size_t kernel_heap_block_payload_offset(void);

static size_t kernel_heap_page_payload_capacity(void);

static struct kernel_heap_page *kernel_heap_page_create(void);

static struct kernel_heap_block *kernel_heap_find_free_block(
    size_t size
);

static void *kernel_heap_block_allocate(
    struct kernel_heap_block *block,
    size_t size
);

static struct kernel_heap_block *kernel_heap_block_from_pointer(
    void *pointer
);

static bool kernel_heap_block_belongs_to_page(
    const struct kernel_heap_page *page,
    const struct kernel_heap_block *block
);

static bool kernel_heap_pointer_is_allocation(
    void *pointer,
    struct kernel_heap_block **block
);

static void kernel_heap_block_coalesce_forward(
    struct kernel_heap_block *block
);

static struct kernel_heap_block *kernel_heap_block_coalesce(
    struct kernel_heap_block *block
);

static bool kernel_heap_page_is_completely_free(
    const struct kernel_heap_page *page
);

static void kernel_heap_page_reclaim(
    struct kernel_heap_page *page
);

static bool kernel_heap_align_up(
    size_t value,
    size_t alignment,
    size_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (alignment == 0) {
        return false;
    }

    size_t remainder =
        value % alignment;

    if (remainder == 0) {
        *result = value;

        return true;
    }

    size_t adjustment =
        alignment - remainder;

    if (value > SIZE_MAX - adjustment) {
        return false;
    }

    *result =
        value + adjustment;

    return true;
}

static size_t kernel_heap_page_payload_offset(void)
{
    size_t offset;

    if (!kernel_heap_align_up(
        sizeof(struct kernel_heap_page),
        (size_t) KERNEL_HEAP_ALIGNMENT,
        &offset
    )) {
        return MEMORY_FRAME_SIZE;
    }

    return offset;
}

static size_t kernel_heap_block_payload_offset(void)
{
    size_t offset;

    if (!kernel_heap_align_up(
        sizeof(struct kernel_heap_block),
        (size_t) KERNEL_HEAP_ALIGNMENT,
        &offset
    )) {
        return MEMORY_FRAME_SIZE;
    }

    return offset;
}

static size_t kernel_heap_page_payload_capacity(void)
{
    size_t page_payload_offset =
        kernel_heap_page_payload_offset();

    size_t block_payload_offset =
        kernel_heap_block_payload_offset();

    if (
        page_payload_offset >=
        (size_t) MEMORY_FRAME_SIZE
    ) {
        return 0;
    }

    size_t remaining =
        (size_t) MEMORY_FRAME_SIZE -
        page_payload_offset;

    if (
        block_payload_offset >=
        remaining
    ) {
        return 0;
    }

    return
        remaining -
        block_payload_offset;
}

static struct kernel_heap_page *kernel_heap_page_create(void)
{
    uint64_t physical_address;

    if (!physical_alloc_frame(
        &physical_address
    )) {
        return NULL;
    }

    struct kernel_heap_page *page =
        memory_physical_to_virtual(
            physical_address
        );

    size_t page_payload_offset =
        kernel_heap_page_payload_offset();

    size_t block_payload_offset =
        kernel_heap_block_payload_offset();

    size_t payload_capacity =
        kernel_heap_page_payload_capacity();

    if (
        page_payload_offset >=
        (size_t) MEMORY_FRAME_SIZE ||
        block_payload_offset >=
        (size_t) MEMORY_FRAME_SIZE ||
        payload_capacity == 0
    ) {
        physical_free_frame(
            physical_address
        );

        return NULL;
    }

    uint8_t *page_bytes =
        (uint8_t *) page;

    struct kernel_heap_block *block =
        (struct kernel_heap_block *) (
            page_bytes +
            page_payload_offset
        );

    block->previous = NULL;
    block->next = NULL;
    block->size = payload_capacity;
    block->free = true;

    page->next = NULL;
    page->physical_address =
        physical_address;
    page->first_block =
        block;

    return page;
}

static struct kernel_heap_block *kernel_heap_find_free_block(
    size_t size)
{
    for (
        struct kernel_heap_page *page =
            kernel_heap_first_page;
        page != NULL;
        page = page->next
    ) {
        for (
            struct kernel_heap_block *block =
                page->first_block;
            block != NULL;
            block = block->next
        ) {
            if (
                block->free &&
                block->size >= size
            ) {
                return block;
            }
        }
    }

    return NULL;
}

static void *kernel_heap_block_allocate(
    struct kernel_heap_block *block,
    size_t size)
{
    if (block == NULL) {
        return NULL;
    }

    if (!block->free) {
        return NULL;
    }

    if (block->size < size) {
        return NULL;
    }

    size_t block_payload_offset =
        kernel_heap_block_payload_offset();

    size_t remaining =
        block->size -
        size;

    if (
        remaining >
        block_payload_offset
    ) {
        uint8_t *block_bytes =
            (uint8_t *) block;

        struct kernel_heap_block *new_block =
            (struct kernel_heap_block *) (
                block_bytes +
                block_payload_offset +
                size
            );

        new_block->previous =
            block;

        new_block->next =
            block->next;

        new_block->size =
            remaining -
            block_payload_offset;

        new_block->free =
            true;

        if (block->next != NULL) {
            block->next->previous =
                new_block;
        }

        block->next =
            new_block;

        block->size =
            size;
    }

    block->free =
        false;

    uint8_t *block_bytes =
        (uint8_t *) block;

    return
        block_bytes +
        block_payload_offset;
}

static struct kernel_heap_block *kernel_heap_block_from_pointer(
    void *pointer)
{
    if (pointer == NULL) {
        return NULL;
    }

    size_t block_payload_offset =
        kernel_heap_block_payload_offset();

    uint8_t *pointer_bytes =
        (uint8_t *) pointer;

    return
        (struct kernel_heap_block *) (
            pointer_bytes -
            block_payload_offset
        );
}

static bool kernel_heap_block_belongs_to_page(
    const struct kernel_heap_page *page,
    const struct kernel_heap_block *block)
{
    if (
        page == NULL ||
        block == NULL
    ) {
        return false;
    }

    uintptr_t page_start =
        (uintptr_t) page;

    uintptr_t page_end =
        page_start +
        (uintptr_t) MEMORY_FRAME_SIZE;

    uintptr_t block_address =
        (uintptr_t) block;

    size_t page_payload_offset =
        kernel_heap_page_payload_offset();

    return
        block_address >=
            page_start +
            page_payload_offset &&
        block_address <
            page_end;
}

static bool kernel_heap_pointer_is_allocation(
    void *pointer,
    struct kernel_heap_block **block)
{
    if (
        pointer == NULL ||
        block == NULL
    ) {
        return false;
    }

    struct kernel_heap_block *candidate =
        kernel_heap_block_from_pointer(
            pointer
        );

    if (candidate == NULL) {
        return false;
    }

    size_t block_payload_offset =
        kernel_heap_block_payload_offset();

    for (
        struct kernel_heap_page *page =
            kernel_heap_first_page;
        page != NULL;
        page = page->next
    ) {
        if (!kernel_heap_block_belongs_to_page(
            page,
            candidate
        )) {
            continue;
        }

        for (
            struct kernel_heap_block *current =
                page->first_block;
            current != NULL;
            current = current->next
        ) {
            if (current != candidate) {
                continue;
            }

            uint8_t *current_bytes =
                (uint8_t *) current;

            if (
                pointer !=
                (
                    void *) (
                        current_bytes +
                        block_payload_offset
                    )
            ) {
                return false;
            }

            *block =
                current;

            return true;
        }

        return false;
    }

    return false;
}

static void kernel_heap_block_coalesce_forward(
    struct kernel_heap_block *block)
{
    if (
        block == NULL ||
        block->next == NULL
    ) {
        return;
    }

    struct kernel_heap_block *next =
        block->next;

    if (
        !block->free ||
        !next->free
    ) {
        return;
    }

    size_t block_payload_offset =
        kernel_heap_block_payload_offset();

    if (
        block->size >
        SIZE_MAX -
        block_payload_offset
    ) {
        kernel_panic(
            "Kernel heap block size overflow during coalescing"
        );
    }

    size_t merged_size =
        block->size +
        block_payload_offset;

    if (
        merged_size >
        SIZE_MAX -
        next->size
    ) {
        kernel_panic(
            "Kernel heap block size overflow during coalescing"
        );
    }

    merged_size +=
        next->size;

    block->size =
        merged_size;

    block->next =
        next->next;

    if (next->next != NULL) {
        next->next->previous =
            block;
    }
}

static struct kernel_heap_block *kernel_heap_block_coalesce(
    struct kernel_heap_block *block)
{
    if (block == NULL) {
        return NULL;
    }

    kernel_heap_block_coalesce_forward(
        block
    );

    if (
        block->previous != NULL &&
        block->previous->free
    ) {
        block =
            block->previous;

        kernel_heap_block_coalesce_forward(
            block
        );
    }

    return block;
}

static bool kernel_heap_page_is_completely_free(
    const struct kernel_heap_page *page)
{
    if (
        page == NULL ||
        page->first_block == NULL
    ) {
        return false;
    }

    const struct kernel_heap_block *block =
        page->first_block;

    if (!block->free) {
        return false;
    }

    if (
        block->previous != NULL ||
        block->next != NULL
    ) {
        return false;
    }

    return
        block->size ==
        kernel_heap_page_payload_capacity();
}

static void kernel_heap_page_reclaim(
    struct kernel_heap_page *page)
{
    if (page == NULL) {
        return;
    }

    /*
     * Keep one permanent backing page so the initialized heap always
     * retains a valid base arena.
     */
    if (page == kernel_heap_first_page) {
        return;
    }

    if (!kernel_heap_page_is_completely_free(page)) {
        return;
    }

    struct kernel_heap_page *previous =
        kernel_heap_first_page;

    while (
        previous != NULL &&
        previous->next != page
    ) {
        previous =
            previous->next;
    }

    if (previous == NULL) {
        kernel_panic(
            "Kernel heap page is not linked"
        );
    }

    struct kernel_heap_page *next =
        page->next;

    uint64_t physical_address =
        page->physical_address;

    previous->next =
        next;

    if (kernel_heap_last_page == page) {
        kernel_heap_last_page =
            previous;
    }

    if (!physical_free_frame(
        physical_address
    )) {
        kernel_panic(
            "Kernel heap failed to release backing frame"
        );
    }
}

bool kernel_heap_init(void)
{
    if (kernel_heap_initialized) {
        return false;
    }

    struct kernel_heap_page *page =
        kernel_heap_page_create();

    if (page == NULL) {
        return false;
    }

    kernel_heap_first_page = page;
    kernel_heap_last_page = page;
    kernel_heap_initialized = true;

    return true;
}

void *kmalloc(size_t size)
{
    if (!kernel_heap_initialized) {
        return NULL;
    }

    if (size == 0) {
        return NULL;
    }

    size_t aligned_size;

    if (!kernel_heap_align_up(
        size,
        (size_t) KERNEL_HEAP_ALIGNMENT,
        &aligned_size
    )) {
        return NULL;
    }

    size_t payload_capacity =
        kernel_heap_page_payload_capacity();

    if (
        aligned_size == 0 ||
        aligned_size > payload_capacity
    ) {
        return NULL;
    }

    struct kernel_heap_block *block =
        kernel_heap_find_free_block(
            aligned_size
        );

    if (block == NULL) {
        struct kernel_heap_page *page =
            kernel_heap_page_create();

        if (page == NULL) {
            return NULL;
        }

        kernel_heap_last_page->next =
            page;

        kernel_heap_last_page =
            page;

        block =
            page->first_block;
    }

    return kernel_heap_block_allocate(
        block,
        aligned_size
    );
}

void kfree(void *pointer)
{
    if (pointer == NULL) {
        return;
    }

    if (!kernel_heap_initialized) {
        kernel_panic(
            "Kernel heap free before initialization"
        );
    }

    struct kernel_heap_block *block;

    if (!kernel_heap_pointer_is_allocation(
        pointer,
        &block
    )) {
        kernel_panic(
            "Kernel heap received invalid free pointer"
        );
    }

    if (block->free) {
        kernel_panic(
            "Kernel heap detected double free"
        );
    }

    struct kernel_heap_page *page = NULL;

    for (
        struct kernel_heap_page *current =
            kernel_heap_first_page;
        current != NULL;
        current = current->next
    ) {
        if (kernel_heap_block_belongs_to_page(
            current,
            block
        )) {
            page = current;
            break;
        }
    }

    if (page == NULL) {
        kernel_panic(
            "Kernel heap allocation page not found"
        );
    }

    block->free = true;

    (void) kernel_heap_block_coalesce(
        block
    );

    if (
        page != kernel_heap_first_page &&
        kernel_heap_page_is_completely_free(
            page
        )
    ) {
        kernel_heap_page_reclaim(
            page
        );
    }
}

bool kernel_heap_stats_get(struct kernel_heap_stats *stats)
{
    if (!kernel_heap_initialized || stats == NULL) {
        return false;
    }

    struct kernel_heap_stats result = {0};

    for (
        struct kernel_heap_page *page = kernel_heap_first_page;
        page != NULL;
        page = page->next
    ) {
        if (result.page_count == SIZE_MAX) {
            return false;
        }

        ++result.page_count;

        for (
            struct kernel_heap_block *block = page->first_block;
            block != NULL;
            block = block->next
        ) {
            if (block->free) {
                if (result.free_block_count == SIZE_MAX) {
                    return false;
                }

                if (result.free_bytes > SIZE_MAX - block->size) {
                    return false;
                }

                ++result.free_block_count;
                result.free_bytes += block->size;
            } else {
                if (result.allocated_block_count == SIZE_MAX) {
                    return false;
                }

                if (result.allocated_bytes > SIZE_MAX - block->size) {
                    return false;
                }

                ++result.allocated_block_count;
                result.allocated_bytes += block->size;
            }
        }
    }

    *stats = result;

    return true;
}

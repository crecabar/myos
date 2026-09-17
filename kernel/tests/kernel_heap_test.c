// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file kernel_heap_test.c
 * @brief Minimal kernel heap regression tests.
 */

#include "kernel_heap_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../memory/heap.h"
#include "../memory/memory.h"

#include <stddef.h>
#include <stdint.h>

static void kernel_heap_test_zero_size(void)
{
    if (kmalloc(0) != NULL) {
        kernel_panic(
            "Kernel heap accepted zero-size allocation"
        );
    }
}

static void kernel_heap_test_alignment_and_overlap(void)
{
    void *first =
        kmalloc(1);

    void *second =
        kmalloc(17);

    void *third =
        kmalloc(31);

    if (
        first == NULL ||
        second == NULL ||
        third == NULL
    ) {
        kernel_panic(
            "Kernel heap basic allocation failed"
        );
    }

    if (
        ((uintptr_t) first & 0xFULL) != 0 ||
        ((uintptr_t) second & 0xFULL) != 0 ||
        ((uintptr_t) third & 0xFULL) != 0
    ) {
        kernel_panic(
            "Kernel heap allocation is not 16-byte aligned"
        );
    }

    uintptr_t first_start =
        (uintptr_t) first;

    uintptr_t second_start =
        (uintptr_t) second;

    uintptr_t third_start =
        (uintptr_t) third;

    if (
        second_start <= first_start ||
        third_start <= second_start
    ) {
        kernel_panic(
            "Kernel heap allocations do not advance monotonically"
        );
    }

    if (
        second_start - first_start < 16 ||
        third_start - second_start < 32
    ) {
        kernel_panic(
            "Kernel heap allocations overlap"
        );
    }
}

static void kernel_heap_test_contents(void)
{
    uint8_t *first =
        kmalloc(64);

    uint8_t *second =
        kmalloc(64);

    if (
        first == NULL ||
        second == NULL
    ) {
        kernel_panic(
            "Kernel heap content test allocation failed"
        );
    }

    for (
        size_t index = 0;
        index < 64;
        ++index
    ) {
        first[index] =
            (uint8_t) index;

        second[index] =
            (uint8_t) (0xFFU - index);
    }

    for (
        size_t index = 0;
        index < 64;
        ++index
    ) {
        if (
            first[index] !=
            (uint8_t) index
        ) {
            kernel_panic(
                "Kernel heap first allocation contents were corrupted"
            );
        }

        if (
            second[index] !=
            (uint8_t) (0xFFU - index)
        ) {
            kernel_panic(
                "Kernel heap second allocation contents were corrupted"
            );
        }
    }
}

static void kernel_heap_test_page_growth(void)
{
    uint64_t free_before =
        physical_free_frame_count();

    void *allocations[64];

    size_t allocation_count = 0;

    while (
        allocation_count <
        (
            sizeof(allocations) /
            sizeof(allocations[0])
        )
    ) {
        void *allocation =
            kmalloc(256);

        if (allocation == NULL) {
            kernel_panic(
                "Kernel heap failed while growing across pages"
            );
        }

        allocations[allocation_count] =
            allocation;

        ++allocation_count;

        if (
            physical_free_frame_count() <
            free_before
        ) {
            break;
        }
    }

    if (
        allocation_count ==
        (
            sizeof(allocations) /
            sizeof(allocations[0])
        )
    ) {
        kernel_panic(
            "Kernel heap did not allocate an additional backing page"
        );
    }

    for (
        size_t index = 0;
        index < allocation_count;
        ++index
    ) {
        if (allocations[index] == NULL) {
            kernel_panic(
                "Kernel heap growth returned null allocation"
            );
        }

        if (
            ((uintptr_t) allocations[index] & 0xFULL) != 0
        ) {
            kernel_panic(
                "Kernel heap growth allocation lost alignment"
            );
        }
    }
}

static void kernel_heap_test_reuse(void)
{
    void *first =
        kmalloc(128);

    void *second =
        kmalloc(128);

    if (
        first == NULL ||
        second == NULL
    ) {
        kernel_panic(
            "Kernel heap reuse test allocation failed"
        );
    }

    kfree(first);

    void *replacement =
        kmalloc(128);

    if (replacement == NULL) {
        kernel_panic(
            "Kernel heap failed to reuse free allocation"
        );
    }

    if (replacement != first) {
        kernel_panic(
            "Kernel heap first-fit did not reuse free block"
        );
    }

    kfree(replacement);
    kfree(second);
}

static void kernel_heap_test_coalescing(void)
{
    void *first =
        kmalloc(256);

    void *second =
        kmalloc(256);

    void *guard =
        kmalloc(256);

    if (
        first == NULL ||
        second == NULL ||
        guard == NULL
    ) {
        kernel_panic(
            "Kernel heap coalescing test allocation failed"
        );
    }

    kfree(first);
    kfree(second);

    void *merged =
        kmalloc(400);

    if (merged == NULL) {
        kernel_panic(
            "Kernel heap failed to allocate from coalesced blocks"
        );
    }

    if (merged != first) {
        kernel_panic(
            "Kernel heap did not reuse coalesced block start"
        );
    }

    kfree(merged);
    kfree(guard);
}

void kernel_heap_test_run(void)
{
    kernel_heap_test_zero_size();
    kernel_heap_test_alignment_and_overlap();
    kernel_heap_test_contents();
    kernel_heap_test_page_growth();
    kernel_heap_test_reuse();
    kernel_heap_test_coalescing();

    diagnostics_write(
        "[heap] Minimal kernel heap test passed\n"
    );
}

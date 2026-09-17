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
    void *first = kmalloc(1);
    void *second = kmalloc(17);
    void *third = kmalloc(31);

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

    uintptr_t first_start = (uintptr_t) first;
    uintptr_t second_start = (uintptr_t) second;
    uintptr_t third_start = (uintptr_t) third;

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

    kfree(third);
    kfree(second);
    kfree(first);
}

static void kernel_heap_test_contents(void)
{
    uint8_t *first = kmalloc(64);
    uint8_t *second = kmalloc(64);

    if (
        first == NULL ||
        second == NULL
    ) {
        kernel_panic(
            "Kernel heap content test allocation failed"
        );
    }

    for (size_t index = 0; index < 64; ++index) {
        first[index] = (uint8_t) index;
        second[index] = (uint8_t) (0xFFU - index);
    }

    for (size_t index = 0; index < 64; ++index) {
        if (first[index] != (uint8_t) index) {
            kernel_panic(
                "Kernel heap first allocation contents were corrupted"
            );
        }

        if (second[index] != (uint8_t) (0xFFU - index)) {
            kernel_panic(
                "Kernel heap second allocation contents were corrupted"
            );
        }
    }

    kfree(second);
    kfree(first);
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

    while (allocation_count > 0) {
        --allocation_count;

        kfree(
            allocations[allocation_count]
        );
    }

    uint64_t free_after =
        physical_free_frame_count();

    if (free_after != free_before) {
        kernel_panic(
            "Kernel heap failed to reclaim backing page"
        );
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

static void kernel_heap_test_lifetime_statistics(void)
{
    struct kernel_heap_stats before;

    if (!kernel_heap_stats_get(&before)) {
        kernel_panic(
            "Kernel heap failed to collect baseline statistics"
        );
    }

    void *first = kmalloc(1);
    void *second = kmalloc(17);
    void *third = kmalloc(64);

    if (
        first == NULL ||
        second == NULL ||
        third == NULL
    ) {
        kernel_panic(
            "Kernel heap lifetime statistics allocation failed"
        );
    }

    struct kernel_heap_stats allocated;

    if (!kernel_heap_stats_get(&allocated)) {
        kernel_panic(
            "Kernel heap failed to collect allocated statistics"
        );
    }

    if (
        allocated.allocated_block_count !=
        before.allocated_block_count + 3
    ) {
        kernel_panic(
            "Kernel heap allocated block statistics mismatch"
        );
    }

    if (
        allocated.allocated_bytes !=
        before.allocated_bytes + 112
    ) {
        kernel_panic(
            "Kernel heap allocated byte statistics mismatch"
        );
    }

    kfree(second);
    kfree(first);
    kfree(third);

    struct kernel_heap_stats after;

    if (!kernel_heap_stats_get(&after)) {
        kernel_panic(
            "Kernel heap failed to collect final statistics"
        );
    }

    if (
        after.page_count !=
        before.page_count ||
        after.allocated_block_count !=
        before.allocated_block_count ||
        after.allocated_bytes !=
        before.allocated_bytes
    ) {
        kernel_panic(
            "Kernel heap allocation lifetime accounting leaked"
        );
    }
}

static void kernel_heap_test_stress(void)
{
    struct kernel_heap_stats before;

    if (!kernel_heap_stats_get(&before)) {
        kernel_panic(
            "Kernel heap failed to collect stress baseline"
        );
    }

    for (size_t cycle = 0; cycle < 128; ++cycle) {
        uint8_t *first = kmalloc(32);
        uint8_t *second = kmalloc(96);
        uint8_t *third = kmalloc(160);
        uint8_t *fourth = kmalloc(48);

        if (
            first == NULL ||
            second == NULL ||
            third == NULL ||
            fourth == NULL
        ) {
            kernel_panic(
                "Kernel heap stress allocation failed"
            );
        }

        for (size_t index = 0; index < 32; ++index) {
            first[index] = (uint8_t) (cycle + index);
        }

        for (size_t index = 0; index < 96; ++index) {
            second[index] = (uint8_t) (0xA5U ^ index);
        }

        for (size_t index = 0; index < 160; ++index) {
            third[index] = (uint8_t) (cycle ^ index);
        }

        for (size_t index = 0; index < 48; ++index) {
            fourth[index] = (uint8_t) (0x5AU + index);
        }

        for (size_t index = 0; index < 32; ++index) {
            if (first[index] != (uint8_t) (cycle + index)) {
                kernel_panic(
                    "Kernel heap stress first allocation corrupted"
                );
            }
        }

        for (size_t index = 0; index < 96; ++index) {
            if (second[index] != (uint8_t) (0xA5U ^ index)) {
                kernel_panic(
                    "Kernel heap stress second allocation corrupted"
                );
            }
        }

        for (size_t index = 0; index < 160; ++index) {
            if (third[index] != (uint8_t) (cycle ^ index)) {
                kernel_panic(
                    "Kernel heap stress third allocation corrupted"
                );
            }
        }

        for (size_t index = 0; index < 48; ++index) {
            if (fourth[index] != (uint8_t) (0x5AU + index)) {
                kernel_panic(
                    "Kernel heap stress fourth allocation corrupted"
                );
            }
        }

        /*
         * Deliberately free out of allocation order so both forward and
         * backward coalescing paths are exercised repeatedly.
         */
        kfree(second);
        kfree(fourth);
        kfree(first);
        kfree(third);
    }

    struct kernel_heap_stats after;

    if (!kernel_heap_stats_get(&after)) {
        kernel_panic(
            "Kernel heap failed to collect stress result"
        );
    }

    if (
        after.page_count !=
        before.page_count ||
        after.allocated_block_count !=
        before.allocated_block_count ||
        after.allocated_bytes !=
        before.allocated_bytes
    ) {
        kernel_panic(
            "Kernel heap stress test leaked allocations"
        );
    }
}

static void kernel_heap_test_multiple_page_reclaim(void)
{
    struct kernel_heap_stats before;

    if (!kernel_heap_stats_get(
        &before
    )) {
        kernel_panic(
            "Kernel heap failed to collect multi-page baseline"
        );
    }

    uint64_t frames_before =
        physical_free_frame_count();

    void *allocations[48];

    for (
        size_t index = 0;
        index < 48;
        ++index
    ) {
        allocations[index] =
            kmalloc(256);

        if (allocations[index] == NULL) {
            kernel_panic(
                "Kernel heap multi-page allocation failed"
            );
        }
    }

    struct kernel_heap_stats expanded;

    if (!kernel_heap_stats_get(
        &expanded
    )) {
        kernel_panic(
            "Kernel heap failed to collect expanded statistics"
        );
    }

    if (
        expanded.page_count <
        before.page_count + 2
    ) {
        kernel_panic(
            "Kernel heap multi-page test did not grow enough"
        );
    }

    for (
        size_t index = 0;
        index < 48;
        index += 2
    ) {
        kfree(
            allocations[index]
        );
    }

    for (
        size_t index = 1;
        index < 48;
        index += 2
    ) {
        kfree(
            allocations[index]
        );
    }

    struct kernel_heap_stats after;

    if (!kernel_heap_stats_get(
        &after
    )) {
        kernel_panic(
            "Kernel heap failed to collect reclaimed statistics"
        );
    }

    if (
        after.page_count !=
        before.page_count
    ) {
        kernel_panic(
            "Kernel heap did not reclaim temporary pages"
        );
    }

    if (
        physical_free_frame_count() !=
        frames_before
    ) {
        kernel_panic(
            "Kernel heap leaked physical backing frames"
        );
    }
}

void kernel_heap_test_run(void)
{
    kernel_heap_test_zero_size();
    kernel_heap_test_alignment_and_overlap();
    kernel_heap_test_contents();
    kernel_heap_test_page_growth();
    kernel_heap_test_reuse();
    kernel_heap_test_coalescing();
    kernel_heap_test_lifetime_statistics();
    kernel_heap_test_stress();
    kernel_heap_test_multiple_page_reclaim();

    diagnostics_write(
        "[heap] Minimal kernel heap test passed\n"
    );
}

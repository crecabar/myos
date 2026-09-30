// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "user_copy_test.h"

#include "../arch/x86_64/paging.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../memory/memory.h"
#include "../process/memory.h"
#include "../process/user_copy.h"

#include <stddef.h>
#include <stdint.h>

#define USER_COPY_TEST_BASE             0x0000000000600000ULL
#define USER_COPY_TEST_READ_ONLY        0x0000000000610000ULL
#define USER_COPY_TEST_KERNEL_ONLY      0x0000000000620000ULL
#define USER_COPY_TEST_PARTIAL          0x0000000000630000ULL

// PRIVATE FUNCTIONS AND HELPERS DECLARATIONS
static void user_copy_test_cross_page(void);
static void user_copy_test_read_only(void);
static void user_copy_test_kernel_only(void);
static void user_copy_test_all_or_nothing(void);
static void user_copy_test_invalid_ranges(void);

// PUBLIC FUNCTIONS IMPLEMENTATIONS
void user_copy_test_run(void)
{
    user_copy_test_cross_page();
    user_copy_test_read_only();
    user_copy_test_kernel_only();
    user_copy_test_all_or_nothing();
    user_copy_test_invalid_ranges();

    diagnostics_write(
        "[process] User copy boundary tests passed\n"
    );
}

// PRIVATE FUNCTIONS AND HELPERS IMPLEMENTATIONS
static void user_copy_test_cross_page(void)
{
    uint64_t free_before =
        physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create cross-page user-copy address space"
        );
    }

    if (!process_memory_allocate_pages(
        &memory,
        USER_COPY_TEST_BASE,
        2,
        true
    )) {
        kernel_panic(
            "Unable to allocate cross-page user-copy mappings"
        );
    }

    const uint64_t test_address =
        USER_COPY_TEST_BASE + 0xFF0ULL;

    uint8_t source[32];

    for (
        size_t index = 0;
        index < sizeof(source);
        ++index
    ) {
        source[index] =
            (uint8_t) (0x20U + index);
    }

    if (!process_memory_write(
        &memory,
        test_address,
        source,
        sizeof(source)
    )) {
        kernel_panic(
            "Unable to initialize cross-page user-copy source"
        );
    }

    uint8_t copied_from_user[32];

    for (
        size_t index = 0;
        index < sizeof(copied_from_user);
        ++index
    ) {
        copied_from_user[index] = 0;
    }

    if (!copy_from_user(
        &memory,
        test_address,
        copied_from_user,
        sizeof(copied_from_user)
    )) {
        kernel_panic(
            "copy_from_user failed across page boundary"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(source);
        ++index
    ) {
        if (
            copied_from_user[index] !=
            source[index]
        ) {
            kernel_panic(
                "copy_from_user produced incorrect cross-page data"
            );
        }
    }

    uint8_t source_to_user[32];

    for (
        size_t index = 0;
        index < sizeof(source_to_user);
        ++index
    ) {
        source_to_user[index] =
            (uint8_t) (0x80U + index);
    }

    if (!copy_to_user(
        &memory,
        test_address,
        source_to_user,
        sizeof(source_to_user)
    )) {
        kernel_panic(
            "copy_to_user failed across page boundary"
        );
    }

    uint8_t observed[32];

    if (!process_memory_read(
        &memory,
        test_address,
        observed,
        sizeof(observed)
    )) {
        kernel_panic(
            "Unable to inspect cross-page copy_to_user result"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(source_to_user);
        ++index
    ) {
        if (
            observed[index] !=
            source_to_user[index]
        ) {
            kernel_panic(
                "copy_to_user produced incorrect cross-page data"
            );
        }
    }

    if (!process_memory_release_pages(
        &memory,
        USER_COPY_TEST_BASE,
        2
    )) {
        kernel_panic(
            "Unable to release cross-page user-copy mappings"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy cross-page user-copy address space"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "Cross-page user-copy test leaked physical frames"
        );
    }
}

static void user_copy_test_read_only(void)
{
    uint64_t free_before =
        physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create read-only user-copy address space"
        );
    }

    if (!process_memory_allocate_executable_page(
        &memory,
        USER_COPY_TEST_READ_ONLY
    )) {
        kernel_panic(
            "Unable to allocate read-only user mapping"
        );
    }

    static const uint8_t initial[] = {
        0x11,
        0x22,
        0x33,
        0x44,
    };

    if (!process_memory_write(
        &memory,
        USER_COPY_TEST_READ_ONLY,
        initial,
        sizeof(initial)
    )) {
        kernel_panic(
            "Unable to initialize read-only user mapping"
        );
    }

    uint8_t copied[sizeof(initial)];

    if (!copy_from_user(
        &memory,
        USER_COPY_TEST_READ_ONLY,
        copied,
        sizeof(copied)
    )) {
        kernel_panic(
            "copy_from_user rejected readable user page"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(initial);
        ++index
    ) {
        if (copied[index] != initial[index]) {
            kernel_panic(
                "copy_from_user read incorrect read-only data"
            );
        }
    }

    static const uint8_t replacement[] = {
        0xAA,
        0xBB,
        0xCC,
        0xDD,
    };

    if (copy_to_user(
        &memory,
        USER_COPY_TEST_READ_ONLY,
        replacement,
        sizeof(replacement)
    )) {
        kernel_panic(
            "copy_to_user wrote through read-only user page"
        );
    }

    uint8_t observed[sizeof(initial)];

    if (!process_memory_read(
        &memory,
        USER_COPY_TEST_READ_ONLY,
        observed,
        sizeof(observed)
    )) {
        kernel_panic(
            "Unable to inspect rejected read-only user copy"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(initial);
        ++index
    ) {
        if (observed[index] != initial[index]) {
            kernel_panic(
                "Rejected copy_to_user modified read-only page"
            );
        }
    }

    if (!process_memory_release_page(
        &memory,
        USER_COPY_TEST_READ_ONLY
    )) {
        kernel_panic(
            "Unable to release read-only user-copy page"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy read-only user-copy address space"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "Read-only user-copy test leaked physical frames"
        );
    }
}

static void user_copy_test_kernel_only(void)
{
    uint64_t free_before =
        physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create kernel-only user-copy address space"
        );
    }

    uint64_t physical_address;

    if (!physical_alloc_frame(
        &physical_address
    )) {
        kernel_panic(
            "Unable to allocate kernel-only user-copy frame"
        );
    }

    if (!paging_map_page(
        &memory.address_space,
        USER_COPY_TEST_KERNEL_ONLY,
        physical_address,
        true,
        false,
        false
    )) {
        physical_free_frame(
            physical_address
        );

        kernel_panic(
            "Unable to map kernel-only lower-half page"
        );
    }

    uint8_t destination = 0xA5;

    if (copy_from_user(
        &memory,
        USER_COPY_TEST_KERNEL_ONLY,
        &destination,
        sizeof(destination)
    )) {
        kernel_panic(
            "copy_from_user accepted supervisor-only mapping"
        );
    }

    if (destination != 0xA5) {
        kernel_panic(
            "Rejected supervisor copy modified kernel destination"
        );
    }

    const uint8_t source = 0x5A;

    if (copy_to_user(
        &memory,
        USER_COPY_TEST_KERNEL_ONLY,
        &source,
        sizeof(source)
    )) {
        kernel_panic(
            "copy_to_user accepted supervisor-only mapping"
        );
    }

    uint64_t unmapped_physical;

    if (!paging_unmap_page(
        &memory.address_space,
        USER_COPY_TEST_KERNEL_ONLY,
        &unmapped_physical
    )) {
        kernel_panic(
            "Unable to unmap kernel-only lower-half page"
        );
    }

    if (
        unmapped_physical !=
        physical_address
    ) {
        kernel_panic(
            "Kernel-only user-copy unmap returned wrong frame"
        );
    }

    if (!physical_free_frame(
        unmapped_physical
    )) {
        kernel_panic(
            "Unable to release kernel-only user-copy frame"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy kernel-only user-copy address space"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "Kernel-only user-copy test leaked physical frames"
        );
    }
}

static void user_copy_test_all_or_nothing(void)
{
    uint64_t free_before =
        physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create atomic user-copy address space"
        );
    }

    if (!process_memory_allocate_page(
        &memory,
        USER_COPY_TEST_PARTIAL,
        true
    )) {
        kernel_panic(
            "Unable to allocate atomic user-copy page"
        );
    }

    const uint64_t boundary_address =
        USER_COPY_TEST_PARTIAL +
        0xFF0ULL;

    uint8_t initial[16];

    for (
        size_t index = 0;
        index < sizeof(initial);
        ++index
    ) {
        initial[index] = 0xA5;
    }

    if (!process_memory_write(
        &memory,
        boundary_address,
        initial,
        sizeof(initial)
    )) {
        kernel_panic(
            "Unable to initialize atomic user-copy boundary"
        );
    }

    uint8_t kernel_destination[32];

    for (
        size_t index = 0;
        index < sizeof(kernel_destination);
        ++index
    ) {
        kernel_destination[index] = 0xCC;
    }

    if (copy_from_user(
        &memory,
        boundary_address,
        kernel_destination,
        sizeof(kernel_destination)
    )) {
        kernel_panic(
            "copy_from_user accepted partially mapped range"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(kernel_destination);
        ++index
    ) {
        if (
            kernel_destination[index] !=
            0xCC
        ) {
            kernel_panic(
                "Failed copy_from_user partially modified destination"
            );
        }
    }

    uint8_t kernel_source[32];

    for (
        size_t index = 0;
        index < sizeof(kernel_source);
        ++index
    ) {
        kernel_source[index] = 0x5A;
    }

    if (copy_to_user(
        &memory,
        boundary_address,
        kernel_source,
        sizeof(kernel_source)
    )) {
        kernel_panic(
            "copy_to_user accepted partially mapped range"
        );
    }

    uint8_t observed[16];

    if (!process_memory_read(
        &memory,
        boundary_address,
        observed,
        sizeof(observed)
    )) {
        kernel_panic(
            "Unable to inspect failed copy_to_user boundary"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(observed);
        ++index
    ) {
        if (observed[index] != 0xA5) {
            kernel_panic(
                "Failed copy_to_user partially modified userspace"
            );
        }
    }

    if (!process_memory_release_page(
        &memory,
        USER_COPY_TEST_PARTIAL
    )) {
        kernel_panic(
            "Unable to release atomic user-copy page"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy atomic user-copy address space"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "Atomic user-copy test leaked physical frames"
        );
    }
}

static void user_copy_test_invalid_ranges(void)
{
    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create invalid-range user-copy address space"
        );
    }

    uint8_t destination = 0xA5;
    const uint8_t source = 0x5A;

    if (
        copy_from_user(
            &memory,
            0,
            &destination,
            sizeof(destination)
        ) ||
        copy_to_user(
            &memory,
            0,
            &source,
            sizeof(source)
        )
    ) {
        kernel_panic(
            "User-copy boundary accepted null userspace address"
        );
    }

    if (
        copy_from_user(
            &memory,
            0xFFFF800000000000ULL,
            &destination,
            sizeof(destination)
        ) ||
        copy_to_user(
            &memory,
            0xFFFF800000000000ULL,
            &source,
            sizeof(source)
        )
    ) {
        kernel_panic(
            "User-copy boundary accepted higher-half address"
        );
    }

    if (
        copy_from_user(
            &memory,
            PROCESS_USER_VIRTUAL_MIN,
            &destination,
            SIZE_MAX
        ) ||
        copy_to_user(
            &memory,
            PROCESS_USER_VIRTUAL_MIN,
            &source,
            SIZE_MAX
        )
    ) {
        kernel_panic(
            "User-copy boundary accepted overflowing range"
        );
    }

    /*
     * Zero-length copies intentionally do not dereference or validate
     * either pointer.
     */
    if (
        !copy_from_user(
            &memory,
            0,
            NULL,
            0
        ) ||
        !copy_to_user(
            &memory,
            UINT64_MAX,
            NULL,
            0
        )
    ) {
        kernel_panic(
            "Zero-length user-copy semantics changed"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy invalid-range user-copy address space"
        );
    }
}

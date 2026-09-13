// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_memory_test.c
 * @brief Process address-space memory regression tests.
 */

#include "process_memory_test.h"

#include "../arch/x86_64/paging.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../memory/memory.h"
#include "../process/layout.h"
#include "../process/memory.h"

#include <stddef.h>
#include <stdint.h>

#define PROCESS_MEMORY_TEST_KERNEL_ADDRESS 0xFFFFFFFF80000000ULL
#define PROCESS_MEMORY_TEST_SHARED_HALF_ADDRESS 0xFFFF800000000000ULL

static void process_memory_test_user_range_policy(void)
{
    if (process_memory_user_range_valid(0x0ULL, 1)) {
        kernel_panic("Null user virtual address was accepted");
    }

    if (process_memory_user_range_valid(
        PROCESS_USER_VIRTUAL_MIN - 1ULL,
        1
    )) {
        kernel_panic("Address below user virtual minimum was accepted");
    }

    if (!process_memory_user_range_valid(
        PROCESS_USER_VIRTUAL_MIN,
        0
    )) {
        kernel_panic("Valid zero-size user range was rejected");
    }

    if (process_memory_user_range_valid(
        0x0ULL,
        0
    )) {
        kernel_panic("Invalid zero-size user range was accepted");
    }

    if (!process_memory_user_range_valid(
        PROCESS_LAYOUT_CODE_BASE,
        64
    )) {
        kernel_panic("Valid user virtual range was rejected");
    }

    if (!process_memory_user_range_valid(
        PROCESS_USER_VIRTUAL_MAX - 15ULL,
        16
    )) {
        kernel_panic("Range ending at user virtual maximum was rejected");
    }

    if (process_memory_user_range_valid(
        PROCESS_USER_VIRTUAL_MAX - 7ULL,
        9
    )) {
        kernel_panic("Range crossing user virtual maximum was accepted");
    }

    if (process_memory_user_range_valid(
        PROCESS_USER_VIRTUAL_MIN,
        SIZE_MAX
    )) {
        kernel_panic("Overflowing user virtual range was accepted");
    }

    if (process_memory_user_range_valid(
        PROCESS_MEMORY_TEST_KERNEL_ADDRESS,
        1
    )) {
        kernel_panic("Kernel higher-half address was accepted as userspace");
    }

    diagnostics_write(
        "[process] User virtual range policy test passed\n"
    );
}

static void process_memory_test_invalid_mutations(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create invalid mutation test address space"
        );
    }

    uint16_t kernel_pml4_index =
        paging_pml4_index(
            PROCESS_MEMORY_TEST_KERNEL_ADDRESS
        );

    uint64_t kernel_pml4_entry_before =
        memory.address_space.pml4_virtual[kernel_pml4_index];

    uint64_t test_physical_address;

    if (!physical_alloc_frame(&test_physical_address)) {
        kernel_panic(
            "Unable to allocate invalid mutation test frame"
        );
    }

    uint64_t free_before_high_half_map =
        physical_free_frame_count();

    if (process_memory_map_page(
        &memory,
        PROCESS_MEMORY_TEST_KERNEL_ADDRESS,
        test_physical_address,
        true
    )) {
        kernel_panic(
            "Process memory mapped a higher-half kernel address"
        );
    }

    if (
        memory.address_space.pml4_virtual[kernel_pml4_index] !=
        kernel_pml4_entry_before
    ) {
        kernel_panic(
            "Rejected higher-half mapping modified the process PML4"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before_high_half_map
    ) {
        kernel_panic(
            "Rejected higher-half mapping allocated paging frames"
        );
    }

    uint64_t unmapped_physical_address = UINT64_MAX;

    if (process_memory_unmap_page(
        &memory,
        PROCESS_MEMORY_TEST_KERNEL_ADDRESS,
        &unmapped_physical_address
    )) {
        kernel_panic(
            "Process memory unmapped a higher-half kernel address"
        );
    }

    if (unmapped_physical_address != UINT64_MAX) {
        kernel_panic(
            "Rejected higher-half unmap modified its output"
        );
    }

    if (
        memory.address_space.pml4_virtual[kernel_pml4_index] !=
        kernel_pml4_entry_before
    ) {
        kernel_panic(
            "Rejected higher-half unmap modified the process PML4"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before_high_half_map
    ) {
        kernel_panic(
            "Rejected higher-half unmap changed physical frame ownership"
        );
    }

    if (process_memory_allocate_executable_page(
        &memory,
        PROCESS_MEMORY_TEST_KERNEL_ADDRESS
    )) {
        kernel_panic(
            "Process memory allocated executable higher-half page"
        );
    }

    if (
        memory.address_space.pml4_virtual[kernel_pml4_index] !=
        kernel_pml4_entry_before
    ) {
        kernel_panic(
            "Rejected executable higher-half mapping modified process PML4"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before_high_half_map
    ) {
        kernel_panic(
            "Rejected executable higher-half mapping allocated frames"
        );
    }

    if (!physical_free_frame(test_physical_address)) {
        kernel_panic(
            "Unable to release invalid mutation test frame"
        );
    }

    uint64_t free_before_invalid_range =
        physical_free_frame_count();

    uint64_t last_user_page =
        PROCESS_USER_VIRTUAL_MAX & ~0xFFFULL;

    if (process_memory_allocate_pages(
        &memory,
        last_user_page,
        2,
        true
    )) {
        kernel_panic(
            "Process memory allocated a range crossing userspace limit"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before_invalid_range
    ) {
        kernel_panic(
            "Rejected userspace range allocated physical frames"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy invalid mutation test address space"
        );
    }

    if (physical_free_frame_count() != free_before) {
        kernel_panic(
            "Invalid process memory tests leaked physical frames"
        );
    }

    diagnostics_write(
        "[process] Invalid user memory mutation test passed\n"
    );
}

static void process_memory_test_paging_shared_half_boundary(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct paging_address_space *kernel_space =
        paging_kernel_address_space();

    if (kernel_space == NULL) {
        kernel_panic(
            "Kernel paging address space is unavailable"
        );
    }

    if (kernel_space->kernel_half_shared) {
        kernel_panic(
            "Kernel address space incorrectly marks kernel half as shared"
        );
    }

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create shared-half paging test address space"
        );
    }

    if (!memory.address_space.kernel_half_shared) {
        kernel_panic(
            "Process address space did not mark kernel half as shared"
        );
    }

    uint16_t pml4_index =
        paging_pml4_index(
            PROCESS_MEMORY_TEST_SHARED_HALF_ADDRESS
        );

    if (pml4_index != 256) {
        kernel_panic(
            "Shared-half test address does not use PML4 entry 256"
        );
    }

    uint16_t kernel_pml4_index =
        paging_pml4_index(
            PROCESS_MEMORY_TEST_KERNEL_ADDRESS
        );

    if (kernel_pml4_index != 511) {
        kernel_panic(
            "Kernel test address does not use PML4 entry 511"
        );
    }

    uint64_t kernel_pml4_entry_before =
        memory.address_space.pml4_virtual[kernel_pml4_index];

    if ((kernel_pml4_entry_before & PAGE_ENTRY_PRESENT) == 0) {
        kernel_panic(
            "Kernel higher-half PML4 entry is not present"
        );
    }

    uint64_t pml4_entry_before =
        memory.address_space.pml4_virtual[pml4_index];

    uint64_t test_physical_address;

    if (!physical_alloc_frame(&test_physical_address)) {
        kernel_panic(
            "Unable to allocate shared-half paging test frame"
        );
    }

    uint64_t free_before_map =
        physical_free_frame_count();

    if (paging_map_page(
        &memory.address_space,
        PROCESS_MEMORY_TEST_SHARED_HALF_ADDRESS,
        test_physical_address,
        true,
        true,
        false
    )) {
        kernel_panic(
            "Paging mapped through shared kernel PML4 entry"
        );
    }

    if (
        memory.address_space.pml4_virtual[pml4_index] !=
        pml4_entry_before
    ) {
        kernel_panic(
            "Rejected shared-half mapping modified PML4"
        );
    }

    if (physical_free_frame_count() != free_before_map) {
        kernel_panic(
            "Rejected shared-half mapping allocated paging frames"
        );
    }

    uint64_t unmapped_physical_address = UINT64_MAX;

    if (paging_unmap_page(
        &memory.address_space,
        PROCESS_MEMORY_TEST_KERNEL_ADDRESS,
        &unmapped_physical_address
    )) {
        kernel_panic(
            "Paging unmapped through shared kernel PML4 entry"
        );
    }

    if (unmapped_physical_address != UINT64_MAX) {
        kernel_panic(
            "Rejected shared-half unmap modified its output"
        );
    }

    if (
        memory.address_space.pml4_virtual[kernel_pml4_index] !=
        kernel_pml4_entry_before
    ) {
        kernel_panic(
            "Rejected shared-half unmap modified kernel PML4 entry"
        );
    }

    if (physical_free_frame_count() != free_before_map) {
        kernel_panic(
            "Rejected shared-half unmap changed frame ownership"
        );
    }

    if (!physical_free_frame(test_physical_address)) {
        kernel_panic(
            "Unable to release shared-half paging test frame"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy shared-half paging test address space"
        );
    }

    if (physical_free_frame_count() != free_before) {
        kernel_panic(
            "Shared-half paging test leaked physical frames"
        );
    }

    diagnostics_write(
        "[paging] Shared kernel PML4 boundary test passed\n"
    );
}

void process_memory_test_run(void)
{
    process_memory_test_user_range_policy();
    process_memory_test_invalid_mutations();
    process_memory_test_paging_shared_half_boundary();

    struct process_memory memory;
    struct process_layout layout;

    static const uint8_t source[] = "Hello from user memory";
    uint8_t destination[sizeof(source)];

    if (!process_memory_create(&memory)) {
        kernel_panic("Unable to create process memory test address space");
    }

    if (!process_layout_create(
        &memory,
        &layout
    )) {
        kernel_panic("Unable to create process memory test layout");
    }

    if (!process_memory_write(
        &memory,
        layout.code_base,
        source,
        sizeof(source)
    )) {
        kernel_panic("Unable to write process memory test");
    }

    if (!process_memory_read(
        &memory,
        layout.code_base,
        destination,
        sizeof(destination)
    )) {
        kernel_panic("Unable to read process memory test");
    }

    for (size_t index = 0; index < sizeof(source); ++index) {
        if (source[index] != destination[index]) {
            kernel_panic("Process memory copy test failed");
        }
    }

    if (!process_layout_destroy(
        &memory,
        &layout
    )) {
        kernel_panic("Unable to destroy process memory test layout");
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic("Unable to destroy process memory test address space");
    }

    diagnostics_write("[process] User memory copy test passed\n");
}

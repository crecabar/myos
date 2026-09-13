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

#define PROCESS_MEMORY_TEST_SUPERVISOR_BRANCH_ADDRESS 0x0000000040000000ULL
#define PROCESS_MEMORY_TEST_USER_PML4_TARGET_ADDRESS  0x0000000080000000ULL

#define PROCESS_MEMORY_TEST_USER_BRANCH_ADDRESS       0x0000000040000000ULL
#define PROCESS_MEMORY_TEST_SUPERVISOR_PD_ADDRESS     0x0000000040200000ULL
#define PROCESS_MEMORY_TEST_USER_PD_TARGET_ADDRESS    0x0000000040201000ULL

#define PROCESS_MEMORY_TEST_HUGE_1G_ANCHOR_ADDRESS 0x0000000040000000ULL
#define PROCESS_MEMORY_TEST_HUGE_1G_TARGET_ADDRESS 0x0000000040200000ULL

#define PROCESS_MEMORY_TEST_HUGE_2M_ANCHOR_ADDRESS 0x0000000000200000ULL
#define PROCESS_MEMORY_TEST_HUGE_2M_TARGET_ADDRESS 0x0000000000201000ULL

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

static void process_memory_test_paging_user_pml4_promotion(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct paging_address_space address_space;

    if (!paging_address_space_create(&address_space)) {
        kernel_panic(
            "Unable to create PML4 promotion test address space"
        );
    }

    uint64_t supervisor_physical;

    if (!physical_alloc_frame(&supervisor_physical)) {
        kernel_panic(
            "Unable to allocate supervisor branch test frame"
        );
    }

    if (!paging_map_page(
        &address_space,
        PROCESS_MEMORY_TEST_SUPERVISOR_BRANCH_ADDRESS,
        supervisor_physical,
        true,
        false,
        false
    )) {
        kernel_panic(
            "Unable to create supervisor PML4 branch"
        );
    }

    uint16_t pml4_index =
        paging_pml4_index(
            PROCESS_MEMORY_TEST_SUPERVISOR_BRANCH_ADDRESS
        );

    if (
        pml4_index !=
        paging_pml4_index(
            PROCESS_MEMORY_TEST_USER_PML4_TARGET_ADDRESS
        )
    ) {
        kernel_panic(
            "PML4 promotion test addresses do not share PML4 entry"
        );
    }

    uint64_t pml4_entry_before =
        address_space.pml4_virtual[pml4_index];

    if ((pml4_entry_before & PAGE_ENTRY_PRESENT) == 0) {
        kernel_panic(
            "Supervisor PML4 test entry is not present"
        );
    }

    if ((pml4_entry_before & PAGE_ENTRY_USER) != 0) {
        kernel_panic(
            "Supervisor PML4 test entry is already user-accessible"
        );
    }

    uint64_t user_physical;

    if (!physical_alloc_frame(&user_physical)) {
        kernel_panic(
            "Unable to allocate user PML4 promotion test frame"
        );
    }

    uint64_t free_before_user_map =
        physical_free_frame_count();

    if (paging_map_page(
        &address_space,
        PROCESS_MEMORY_TEST_USER_PML4_TARGET_ADDRESS,
        user_physical,
        true,
        true,
        false
    )) {
        kernel_panic(
            "USER mapping promoted supervisor PML4 branch"
        );
    }

    if (
        address_space.pml4_virtual[pml4_index] !=
        pml4_entry_before
    ) {
        kernel_panic(
            "Rejected USER mapping modified supervisor PML4 entry"
        );
    }

    if (physical_free_frame_count() != free_before_user_map) {
        kernel_panic(
            "Rejected PML4 promotion allocated paging frames"
        );
    }

    struct paging_translation translation;

    if (paging_translate_address_space(
        &address_space,
        PROCESS_MEMORY_TEST_USER_PML4_TARGET_ADDRESS,
        &translation
    )) {
        kernel_panic(
            "Rejected USER PML4 target became mapped"
        );
    }

    if (!physical_free_frame(user_physical)) {
        kernel_panic(
            "Unable to release USER PML4 promotion test frame"
        );
    }

    uint64_t unmapped_physical;

    if (!paging_unmap_page(
        &address_space,
        PROCESS_MEMORY_TEST_SUPERVISOR_BRANCH_ADDRESS,
        &unmapped_physical
    )) {
        kernel_panic(
            "Unable to unmap supervisor PML4 test page"
        );
    }

    if (unmapped_physical != supervisor_physical) {
        kernel_panic(
            "Supervisor PML4 test returned wrong physical frame"
        );
    }

    if (!physical_free_frame(unmapped_physical)) {
        kernel_panic(
            "Unable to release supervisor PML4 test frame"
        );
    }

    if (!paging_address_space_destroy(&address_space)) {
        kernel_panic(
            "Unable to destroy PML4 promotion test address space"
        );
    }

    if (physical_free_frame_count() != free_before) {
        kernel_panic(
            "PML4 promotion test leaked physical frames"
        );
    }

    diagnostics_write(
        "[paging] USER PML4 promotion rejection test passed\n"
    );
}

static void process_memory_test_paging_user_pd_promotion(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct paging_address_space address_space;

    if (!paging_address_space_create(&address_space)) {
        kernel_panic(
            "Unable to create PD promotion test address space"
        );
    }

    uint64_t user_branch_physical;

    if (!physical_alloc_frame(&user_branch_physical)) {
        kernel_panic(
            "Unable to allocate USER branch test frame"
        );
    }

    if (!paging_map_page(
        &address_space,
        PROCESS_MEMORY_TEST_USER_BRANCH_ADDRESS,
        user_branch_physical,
        true,
        true,
        false
    )) {
        kernel_panic(
            "Unable to create USER branch for PD promotion test"
        );
    }

    uint64_t supervisor_physical;

    if (!physical_alloc_frame(&supervisor_physical)) {
        kernel_panic(
            "Unable to allocate supervisor PD test frame"
        );
    }

    if (!paging_map_page(
        &address_space,
        PROCESS_MEMORY_TEST_SUPERVISOR_PD_ADDRESS,
        supervisor_physical,
        true,
        false,
        false
    )) {
        kernel_panic(
            "Unable to create supervisor PD branch"
        );
    }

    uint16_t pml4_index =
        paging_pml4_index(
            PROCESS_MEMORY_TEST_SUPERVISOR_PD_ADDRESS
        );

    uint16_t pdpt_index =
        paging_pdpt_index(
            PROCESS_MEMORY_TEST_SUPERVISOR_PD_ADDRESS
        );

    uint16_t pd_index =
        paging_pd_index(
            PROCESS_MEMORY_TEST_SUPERVISOR_PD_ADDRESS
        );

    uint64_t pml4_entry =
        address_space.pml4_virtual[pml4_index];

    if ((pml4_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic(
            "PD promotion test PML4 entry is not USER"
        );
    }

    uint64_t *pdpt =
        memory_physical_to_virtual(
            paging_entry_address(pml4_entry)
        );

    uint64_t pdpt_entry =
        pdpt[pdpt_index];

    if ((pdpt_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic(
            "PD promotion test PDPT entry is not USER"
        );
    }

    uint64_t *pd =
        memory_physical_to_virtual(
            paging_entry_address(pdpt_entry)
        );

    uint64_t pd_entry_before =
        pd[pd_index];

    if ((pd_entry_before & PAGE_ENTRY_PRESENT) == 0) {
        kernel_panic(
            "Supervisor PD test entry is not present"
        );
    }

    if ((pd_entry_before & PAGE_ENTRY_USER) != 0) {
        kernel_panic(
            "Supervisor PD test entry is already USER"
        );
    }

    uint64_t user_target_physical;

    if (!physical_alloc_frame(&user_target_physical)) {
        kernel_panic(
            "Unable to allocate USER PD promotion test frame"
        );
    }

    uint64_t free_before_user_map =
        physical_free_frame_count();

    if (paging_map_page(
        &address_space,
        PROCESS_MEMORY_TEST_USER_PD_TARGET_ADDRESS,
        user_target_physical,
        true,
        true,
        false
    )) {
        kernel_panic(
            "USER mapping promoted supervisor PD branch"
        );
    }

    if (pd[pd_index] != pd_entry_before) {
        kernel_panic(
            "Rejected USER mapping modified supervisor PD entry"
        );
    }

    if (physical_free_frame_count() != free_before_user_map) {
        kernel_panic(
            "Rejected PD promotion allocated paging frames"
        );
    }

    struct paging_translation translation;

    if (paging_translate_address_space(
        &address_space,
        PROCESS_MEMORY_TEST_USER_PD_TARGET_ADDRESS,
        &translation
    )) {
        kernel_panic(
            "Rejected USER PD target became mapped"
        );
    }

    if (!physical_free_frame(user_target_physical)) {
        kernel_panic(
            "Unable to release USER PD promotion test frame"
        );
    }

    uint64_t unmapped_physical;

    if (!paging_unmap_page(
        &address_space,
        PROCESS_MEMORY_TEST_SUPERVISOR_PD_ADDRESS,
        &unmapped_physical
    )) {
        kernel_panic(
            "Unable to unmap supervisor PD test page"
        );
    }

    if (unmapped_physical != supervisor_physical) {
        kernel_panic(
            "Supervisor PD test returned wrong physical frame"
        );
    }

    if (!physical_free_frame(unmapped_physical)) {
        kernel_panic(
            "Unable to release supervisor PD test frame"
        );
    }

    if (!paging_unmap_page(
        &address_space,
        PROCESS_MEMORY_TEST_USER_BRANCH_ADDRESS,
        &unmapped_physical
    )) {
        kernel_panic(
            "Unable to unmap USER branch test page"
        );
    }

    if (unmapped_physical != user_branch_physical) {
        kernel_panic(
            "USER branch test returned wrong physical frame"
        );
    }

    if (!physical_free_frame(unmapped_physical)) {
        kernel_panic(
            "Unable to release USER branch test frame"
        );
    }

    if (!paging_address_space_destroy(&address_space)) {
        kernel_panic(
            "Unable to destroy PD promotion test address space"
        );
    }

    if (physical_free_frame_count() != free_before) {
        kernel_panic(
            "PD promotion test leaked physical frames"
        );
    }

    diagnostics_write(
        "[paging] USER PD promotion rejection test passed\n"
    );
}

static void process_memory_test_paging_1g_huge_guard(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct paging_address_space address_space;

    if (!paging_address_space_create(&address_space)) {
        kernel_panic(
            "Unable to create 1 GiB huge-page guard address space"
        );
    }

    uint64_t anchor_physical;

    if (!physical_alloc_frame(&anchor_physical)) {
        kernel_panic(
            "Unable to allocate 1 GiB huge-page anchor frame"
        );
    }

    if (!paging_map_page(
        &address_space,
        PROCESS_MEMORY_TEST_HUGE_1G_ANCHOR_ADDRESS,
        anchor_physical,
        true,
        false,
        false
    )) {
        kernel_panic(
            "Unable to create 1 GiB huge-page anchor mapping"
        );
    }

    uint16_t pml4_index =
        paging_pml4_index(
            PROCESS_MEMORY_TEST_HUGE_1G_ANCHOR_ADDRESS
        );

    uint16_t pdpt_index =
        paging_pdpt_index(
            PROCESS_MEMORY_TEST_HUGE_1G_ANCHOR_ADDRESS
        );

    uint16_t anchor_pd_index =
        paging_pd_index(
            PROCESS_MEMORY_TEST_HUGE_1G_ANCHOR_ADDRESS
        );

    uint16_t target_pd_index =
        paging_pd_index(
            PROCESS_MEMORY_TEST_HUGE_1G_TARGET_ADDRESS
        );

    if (anchor_pd_index == target_pd_index) {
        kernel_panic(
            "1 GiB huge-page test addresses use same PD entry"
        );
    }

    uint64_t pml4_entry =
        address_space.pml4_virtual[pml4_index];

    uint64_t *pdpt =
        memory_physical_to_virtual(
            paging_entry_address(pml4_entry)
        );

    uint64_t pdpt_entry_original =
        pdpt[pdpt_index];

    uint64_t *pd =
        memory_physical_to_virtual(
            paging_entry_address(pdpt_entry_original)
        );

    uint64_t target_pd_entry_before =
        pd[target_pd_index];

    uint64_t anchor_pd_entry =
        pd[anchor_pd_index];

    uint64_t *anchor_pt =
        memory_physical_to_virtual(
            paging_entry_address(anchor_pd_entry)
        );

    uint16_t anchor_pt_index =
        paging_pt_index(
            PROCESS_MEMORY_TEST_HUGE_1G_ANCHOR_ADDRESS
        );

    uint64_t anchor_page_entry_before =
        anchor_pt[anchor_pt_index];

    /*
     * The entry deliberately keeps pointing at the existing PD frame.
     * The address space is never activated. This creates a safe trap for
     * detecting a 4 KiB walker that incorrectly descends through HUGE.
     */
    pdpt[pdpt_index] =
        pdpt_entry_original |
        PAGE_ENTRY_HUGE;

    uint64_t huge_entry_before =
        pdpt[pdpt_index];

    uint64_t target_physical;

    if (!physical_alloc_frame(&target_physical)) {
        kernel_panic(
            "Unable to allocate 1 GiB huge-page target frame"
        );
    }

    uint64_t free_before_map =
        physical_free_frame_count();

    if (paging_map_page(
        &address_space,
        PROCESS_MEMORY_TEST_HUGE_1G_TARGET_ADDRESS,
        target_physical,
        true,
        false,
        false
    )) {
        kernel_panic(
            "4 KiB mapping traversed 1 GiB huge-page entry"
        );
    }

    if (pdpt[pdpt_index] != huge_entry_before) {
        kernel_panic(
            "Rejected 1 GiB mapping modified PDPT entry"
        );
    }

    if (pd[target_pd_index] != target_pd_entry_before) {
        kernel_panic(
            "Rejected 1 GiB mapping modified trap PD"
        );
    }

    if (physical_free_frame_count() != free_before_map) {
        kernel_panic(
            "Rejected 1 GiB mapping allocated paging frames"
        );
    }

    struct paging_translation translation;

    if (!paging_translate_address_space(
        &address_space,
        PROCESS_MEMORY_TEST_HUGE_1G_TARGET_ADDRESS,
        &translation
    )) {
        kernel_panic(
            "1 GiB huge-page translation was rejected"
        );
    }

    if (translation.page_size != PAGING_PAGE_SIZE_1G) {
        kernel_panic(
            "1 GiB huge-page translation reported wrong page size"
        );
    }

    uint64_t unmapped_physical = UINT64_MAX;
    uint64_t free_before_unmap =
        physical_free_frame_count();

    if (paging_unmap_page(
        &address_space,
        PROCESS_MEMORY_TEST_HUGE_1G_ANCHOR_ADDRESS,
        &unmapped_physical
    )) {
        kernel_panic(
            "4 KiB unmap traversed 1 GiB huge-page entry"
        );
    }

    if (unmapped_physical != UINT64_MAX) {
        kernel_panic(
            "Rejected 1 GiB unmap modified its output"
        );
    }

    if (pdpt[pdpt_index] != huge_entry_before) {
        kernel_panic(
            "Rejected 1 GiB unmap modified PDPT entry"
        );
    }

    if (
        anchor_pt[anchor_pt_index] !=
        anchor_page_entry_before
    ) {
        kernel_panic(
            "Rejected 1 GiB unmap modified trap page table"
        );
    }

    if (physical_free_frame_count() != free_before_unmap) {
        kernel_panic(
            "Rejected 1 GiB unmap changed frame ownership"
        );
    }

    if (!physical_free_frame(target_physical)) {
        kernel_panic(
            "Unable to release 1 GiB huge-page target frame"
        );
    }

    /*
     * Restore the valid 4 KiB hierarchy before normal cleanup.
     */
    pdpt[pdpt_index] = pdpt_entry_original;

    if (!paging_unmap_page(
        &address_space,
        PROCESS_MEMORY_TEST_HUGE_1G_ANCHOR_ADDRESS,
        &unmapped_physical
    )) {
        kernel_panic(
            "Unable to clean up 1 GiB huge-page anchor"
        );
    }

    if (unmapped_physical != anchor_physical) {
        kernel_panic(
            "1 GiB huge-page cleanup returned wrong frame"
        );
    }

    if (!physical_free_frame(unmapped_physical)) {
        kernel_panic(
            "Unable to release 1 GiB huge-page anchor frame"
        );
    }

    if (!paging_address_space_destroy(&address_space)) {
        kernel_panic(
            "Unable to destroy 1 GiB huge-page guard address space"
        );
    }

    if (physical_free_frame_count() != free_before) {
        kernel_panic(
            "1 GiB huge-page guard test leaked frames"
        );
    }

    diagnostics_write(
        "[paging] 1 GiB huge-page guard test passed\n"
    );
}

static void process_memory_test_paging_2m_huge_guard(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct paging_address_space address_space;

    if (!paging_address_space_create(&address_space)) {
        kernel_panic(
            "Unable to create 2 MiB huge-page guard address space"
        );
    }

    uint64_t anchor_physical;

    if (!physical_alloc_frame(&anchor_physical)) {
        kernel_panic(
            "Unable to allocate 2 MiB huge-page anchor frame"
        );
    }

    if (!paging_map_page(
        &address_space,
        PROCESS_MEMORY_TEST_HUGE_2M_ANCHOR_ADDRESS,
        anchor_physical,
        true,
        false,
        false
    )) {
        kernel_panic(
            "Unable to create 2 MiB huge-page anchor mapping"
        );
    }

    uint16_t pml4_index =
        paging_pml4_index(
            PROCESS_MEMORY_TEST_HUGE_2M_ANCHOR_ADDRESS
        );

    uint16_t pdpt_index =
        paging_pdpt_index(
            PROCESS_MEMORY_TEST_HUGE_2M_ANCHOR_ADDRESS
        );

    uint16_t pd_index =
        paging_pd_index(
            PROCESS_MEMORY_TEST_HUGE_2M_ANCHOR_ADDRESS
        );

    uint16_t anchor_pt_index =
        paging_pt_index(
            PROCESS_MEMORY_TEST_HUGE_2M_ANCHOR_ADDRESS
        );

    uint16_t target_pt_index =
        paging_pt_index(
            PROCESS_MEMORY_TEST_HUGE_2M_TARGET_ADDRESS
        );

    if (anchor_pt_index == target_pt_index) {
        kernel_panic(
            "2 MiB huge-page test addresses use same PT entry"
        );
    }

    uint64_t pml4_entry =
        address_space.pml4_virtual[pml4_index];

    uint64_t *pdpt =
        memory_physical_to_virtual(
            paging_entry_address(pml4_entry)
        );

    uint64_t pdpt_entry =
        pdpt[pdpt_index];

    uint64_t *pd =
        memory_physical_to_virtual(
            paging_entry_address(pdpt_entry)
        );

    uint64_t pd_entry_original =
        pd[pd_index];

    uint64_t *pt =
        memory_physical_to_virtual(
            paging_entry_address(pd_entry_original)
        );

    uint64_t anchor_page_entry_before =
        pt[anchor_pt_index];

    uint64_t target_page_entry_before =
        pt[target_pt_index];

    /*
     * Keep the existing PT as a trap table while presenting its parent PDE
     * to the paging walker as a synthetic 2 MiB huge-page entry.
     */
    pd[pd_index] =
        pd_entry_original |
        PAGE_ENTRY_HUGE;

    uint64_t huge_entry_before =
        pd[pd_index];

    uint64_t target_physical;

    if (!physical_alloc_frame(&target_physical)) {
        kernel_panic(
            "Unable to allocate 2 MiB huge-page target frame"
        );
    }

    uint64_t free_before_map =
        physical_free_frame_count();

    if (paging_map_page(
        &address_space,
        PROCESS_MEMORY_TEST_HUGE_2M_TARGET_ADDRESS,
        target_physical,
        true,
        false,
        false
    )) {
        kernel_panic(
            "4 KiB mapping traversed 2 MiB huge-page entry"
        );
    }

    if (pd[pd_index] != huge_entry_before) {
        kernel_panic(
            "Rejected 2 MiB mapping modified PD entry"
        );
    }

    if (pt[target_pt_index] != target_page_entry_before) {
        kernel_panic(
            "Rejected 2 MiB mapping modified trap page table"
        );
    }

    if (physical_free_frame_count() != free_before_map) {
        kernel_panic(
            "Rejected 2 MiB mapping changed frame ownership"
        );
    }

    struct paging_translation translation;

    if (!paging_translate_address_space(
        &address_space,
        PROCESS_MEMORY_TEST_HUGE_2M_TARGET_ADDRESS,
        &translation
    )) {
        kernel_panic(
            "2 MiB huge-page translation was rejected"
        );
    }

    if (translation.page_size != PAGING_PAGE_SIZE_2M) {
        kernel_panic(
            "2 MiB huge-page translation reported wrong page size"
        );
    }

    uint64_t unmapped_physical = UINT64_MAX;
    uint64_t free_before_unmap =
        physical_free_frame_count();

    if (paging_unmap_page(
        &address_space,
        PROCESS_MEMORY_TEST_HUGE_2M_ANCHOR_ADDRESS,
        &unmapped_physical
    )) {
        kernel_panic(
            "4 KiB unmap traversed 2 MiB huge-page entry"
        );
    }

    if (unmapped_physical != UINT64_MAX) {
        kernel_panic(
            "Rejected 2 MiB unmap modified its output"
        );
    }

    if (pd[pd_index] != huge_entry_before) {
        kernel_panic(
            "Rejected 2 MiB unmap modified PD entry"
        );
    }

    if (
        pt[anchor_pt_index] !=
        anchor_page_entry_before
    ) {
        kernel_panic(
            "Rejected 2 MiB unmap modified trap page table"
        );
    }

    if (physical_free_frame_count() != free_before_unmap) {
        kernel_panic(
            "Rejected 2 MiB unmap changed frame ownership"
        );
    }

    if (!physical_free_frame(target_physical)) {
        kernel_panic(
            "Unable to release 2 MiB huge-page target frame"
        );
    }

    /*
     * Restore the original hierarchy so the normal 4 KiB unmap path can
     * reclaim every intermediate table created for the anchor mapping.
     */
    pd[pd_index] = pd_entry_original;

    if (!paging_unmap_page(
        &address_space,
        PROCESS_MEMORY_TEST_HUGE_2M_ANCHOR_ADDRESS,
        &unmapped_physical
    )) {
        kernel_panic(
            "Unable to clean up 2 MiB huge-page anchor"
        );
    }

    if (unmapped_physical != anchor_physical) {
        kernel_panic(
            "2 MiB huge-page cleanup returned wrong frame"
        );
    }

    if (!physical_free_frame(unmapped_physical)) {
        kernel_panic(
            "Unable to release 2 MiB huge-page anchor frame"
        );
    }

    if (!paging_address_space_destroy(&address_space)) {
        kernel_panic(
            "Unable to destroy 2 MiB huge-page guard address space"
        );
    }

    if (physical_free_frame_count() != free_before) {
        kernel_panic(
            "2 MiB huge-page guard test leaked frames"
        );
    }

    diagnostics_write(
        "[paging] 2 MiB huge-page guard test passed\n"
    );
}

void process_memory_test_run(void)
{
    process_memory_test_user_range_policy();
    process_memory_test_invalid_mutations();
    process_memory_test_paging_shared_half_boundary();
    process_memory_test_paging_user_pml4_promotion();
    process_memory_test_paging_user_pd_promotion();
    process_memory_test_paging_1g_huge_guard();
    process_memory_test_paging_2m_huge_guard();

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

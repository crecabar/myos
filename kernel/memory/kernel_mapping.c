// SPDX-License-Identifier: GPL-2.0-only

#include "kernel_mapping.h"

#include "memory.h"
#include "../arch/x86_64/paging.h"
#include "../core/panic.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_MAPPING_TABLE_ENTRY_COUNT 512

extern char __text_start[];
extern char __text_end[];

extern char __rodata_start[];
extern char __rodata_end[];

extern char __limine_requests_start[];
extern char __limine_requests_end[];

extern char __data_start[];
extern char __data_end[];

extern char __bss_start[];
extern char __bss_end[];

static bool kernel_mapping_map_range(
    struct paging_address_space *address_space,
    uint64_t start,
    uint64_t end,
    bool writable,
    bool executable
);

static bool kernel_mapping_validate_range(
    const struct paging_address_space *address_space,
    uint64_t start,
    uint64_t end,
    bool writable,
    bool executable
);

static bool kernel_mapping_populate(
    struct paging_address_space *address_space
);

static bool kernel_mapping_release_pdpt(
    uint64_t pdpt_physical
);

static bool kernel_mapping_release_pd(
    uint64_t pd_physical
);

static bool kernel_mapping_translation_writable(
    const struct paging_translation *translation
);

static bool kernel_mapping_translation_user(
    const struct paging_translation *translation
);

static bool kernel_mapping_translation_executable(
    const struct paging_translation *translation
);

bool kernel_mapping_build(
    struct paging_address_space *address_space)
{
    if (address_space == NULL) return false;

    struct paging_address_space candidate;

    if (!paging_address_space_create(
        &candidate
    )) {
        return false;
    }

    if (!kernel_mapping_populate(
        &candidate
    )) {
        if (!kernel_mapping_destroy(
            &candidate
        )) {
            kernel_panic(
                "Unable to roll back failed kernel mapping build"
            );
        }

        return false;
    }

    *address_space = candidate;

    return true;
}

bool kernel_mapping_destroy(
    struct paging_address_space *address_space)
{
    if (address_space == NULL) return false;
    if (address_space->pml4_virtual == NULL) return false;
    if (address_space->kernel_half_shared) return false;

    uint16_t kernel_pml4_index =
        paging_pml4_index(
            (uint64_t) __text_start
        );

    for (
        size_t index = 0;
        index < KERNEL_MAPPING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        if (index == kernel_pml4_index) {
            continue;
        }

        if (
            (address_space->pml4_virtual[index] &
             PAGE_ENTRY_PRESENT) != 0
        ) {
            return false;
        }
    }

    uint64_t kernel_entry =
        address_space->pml4_virtual[
            kernel_pml4_index
        ];

    if (
        (kernel_entry &
         PAGE_ENTRY_PRESENT) != 0
    ) {
        if (
            (kernel_entry &
             PAGE_ENTRY_HUGE) != 0
        ) {
            return false;
        }

        uint64_t pdpt_physical =
            paging_entry_address(
                kernel_entry
            );

        if (!kernel_mapping_release_pdpt(
            pdpt_physical
        )) {
            return false;
        }

        address_space->pml4_virtual[
            kernel_pml4_index
        ] = 0;
    }

    return paging_address_space_destroy(
        address_space
    );
}

static bool kernel_mapping_map_range(
    struct paging_address_space *address_space,
    uint64_t start,
    uint64_t end,
    bool writable,
    bool executable)
{
    if (address_space == NULL) return false;
    if (end <= start) return false;

    uint64_t first_page =
        start & PAGE_ADDRESS_MASK_4K;

    uint64_t last_page =
        (end - 1) & PAGE_ADDRESS_MASK_4K;

    for (
        uint64_t virtual_address = first_page;
        ;
        virtual_address += MEMORY_FRAME_SIZE
    ) {
        struct paging_translation translation;

        if (!paging_translate(
            virtual_address,
            &translation
        )) {
            return false;
        }

        uint64_t physical_address =
            translation.physical_address &
            PAGE_ADDRESS_MASK_4K;

        if (!paging_map_page(
            address_space,
            virtual_address,
            physical_address,
            writable,
            false,
            executable
        )) {
            return false;
        }

        if (virtual_address == last_page) {
            break;
        }

        if (
            virtual_address >
            UINT64_MAX - MEMORY_FRAME_SIZE
        ) {
            return false;
        }
    }

    return true;
}

static bool kernel_mapping_validate_range(
    const struct paging_address_space *address_space,
    uint64_t start,
    uint64_t end,
    bool writable,
    bool executable)
{
    if (address_space == NULL) return false;
    if (end <= start) return false;

    uint64_t first_page =
        start & PAGE_ADDRESS_MASK_4K;

    uint64_t last_page =
        (end - 1) & PAGE_ADDRESS_MASK_4K;

    for (
        uint64_t virtual_address = first_page;
        ;
        virtual_address += MEMORY_FRAME_SIZE
    ) {
        struct paging_translation active_translation;
        struct paging_translation candidate_translation;

        if (!paging_translate(
            virtual_address,
            &active_translation
        )) {
            return false;
        }

        if (!paging_translate_address_space(
            address_space,
            virtual_address,
            &candidate_translation
        )) {
            return false;
        }

        if (
            active_translation.physical_address !=
            candidate_translation.physical_address
        ) {
            return false;
        }

        if (
            candidate_translation.page_size !=
            PAGING_PAGE_SIZE_4K
        ) {
            return false;
        }

        if (
            kernel_mapping_translation_writable(
                &candidate_translation
            ) != writable
        ) {
            return false;
        }

        if (
            kernel_mapping_translation_user(
                &candidate_translation
            )
        ) {
            return false;
        }

        if (
            kernel_mapping_translation_executable(
                &candidate_translation
            ) != executable
        ) {
            return false;
        }

        if (virtual_address == last_page) {
            break;
        }

        if (
            virtual_address >
            UINT64_MAX - MEMORY_FRAME_SIZE
        ) {
            return false;
        }
    }

    return true;
}

static bool kernel_mapping_populate(
    struct paging_address_space *address_space)
{
    if (address_space == NULL) return false;

    if (!kernel_mapping_map_range(
        address_space,
        (uint64_t) __text_start,
        (uint64_t) __text_end,
        false,
        true
    )) {
        return false;
    }

    if (!kernel_mapping_map_range(
        address_space,
        (uint64_t) __rodata_start,
        (uint64_t) __rodata_end,
        false,
        false
    )) {
        return false;
    }

    if (!kernel_mapping_map_range(
        address_space,
        (uint64_t) __limine_requests_start,
        (uint64_t) __limine_requests_end,
        false,
        false
    )) {
        return false;
    }

    if (!kernel_mapping_map_range(
        address_space,
        (uint64_t) __data_start,
        (uint64_t) __data_end,
        true,
        false
    )) {
        return false;
    }

    if (!kernel_mapping_map_range(
        address_space,
        (uint64_t) __bss_start,
        (uint64_t) __bss_end,
        true,
        false
    )) {
        return false;
    }

    if (!kernel_mapping_validate_range(
        address_space,
        (uint64_t) __text_start,
        (uint64_t) __text_end,
        false,
        true
    )) {
        return false;
    }

    if (!kernel_mapping_validate_range(
        address_space,
        (uint64_t) __rodata_start,
        (uint64_t) __rodata_end,
        false,
        false
    )) {
        return false;
    }

    if (!kernel_mapping_validate_range(
        address_space,
        (uint64_t) __limine_requests_start,
        (uint64_t) __limine_requests_end,
        false,
        false
    )) {
        return false;
    }

    if (!kernel_mapping_validate_range(
        address_space,
        (uint64_t) __data_start,
        (uint64_t) __data_end,
        true,
        false
    )) {
        return false;
    }

    if (!kernel_mapping_validate_range(
        address_space,
        (uint64_t) __bss_start,
        (uint64_t) __bss_end,
        true,
        false
    )) {
        return false;
    }

    uint16_t kernel_pml4_index =
        paging_pml4_index(
            (uint64_t) __text_start
        );

    uint64_t kernel_entry =
        address_space->pml4_virtual[
            kernel_pml4_index
        ];

    if ((kernel_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    if ((kernel_entry & PAGE_ENTRY_USER) != 0) {
        return false;
    }

    for (
        size_t index = 0;
        index < KERNEL_MAPPING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        if (index == kernel_pml4_index) {
            continue;
        }

        if (
            (address_space->pml4_virtual[index] &
             PAGE_ENTRY_PRESENT) != 0
        ) {
            return false;
        }
    }

    return true;
}

static bool kernel_mapping_release_pdpt(
    uint64_t pdpt_physical)
{
    uint64_t *pdpt =
        memory_physical_to_virtual(
            pdpt_physical
        );

    for (
        size_t index = 0;
        index < KERNEL_MAPPING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        uint64_t entry = pdpt[index];

        if (
            (entry &
             PAGE_ENTRY_PRESENT) == 0
        ) {
            continue;
        }

        if (
            (entry &
             PAGE_ENTRY_HUGE) != 0
        ) {
            /*
             * Huge leaves map borrowed physical memory.
             * The mapping disappears with the table; the leaf frame itself
             * is not owned by this candidate.
             */
            pdpt[index] = 0;
            continue;
        }

        uint64_t pd_physical =
            paging_entry_address(entry);

        if (!kernel_mapping_release_pd(
            pd_physical
        )) {
            return false;
        }

        pdpt[index] = 0;
    }

    return physical_free_frame(
        pdpt_physical
    );
}

static bool kernel_mapping_release_pd(
    uint64_t pd_physical)
{
    uint64_t *pd =
        memory_physical_to_virtual(
            pd_physical
        );

    for (
        size_t index = 0;
        index < KERNEL_MAPPING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        uint64_t entry = pd[index];

        if (
            (entry &
             PAGE_ENTRY_PRESENT) == 0
        ) {
            continue;
        }

        if (
            (entry &
             PAGE_ENTRY_HUGE) != 0
        ) {
            /*
             * Huge leaves reference borrowed physical memory.
             */
            pd[index] = 0;
            continue;
        }

        uint64_t pt_physical =
            paging_entry_address(entry);

        /*
         * PTE leaf frames contain the kernel image and are borrowed.
         * Only the PT frame itself belongs to the detached candidate.
         */
        if (!physical_free_frame(
            pt_physical
        )) {
            return false;
        }

        pd[index] = 0;
    }

    return physical_free_frame(
        pd_physical
    );
}

static bool kernel_mapping_translation_writable(
    const struct paging_translation *translation)
{
    if (translation == NULL) return false;

    if (
        (translation->pml4_entry &
         PAGE_ENTRY_WRITABLE) == 0 ||
        (translation->pdpt_entry &
         PAGE_ENTRY_WRITABLE) == 0
    ) {
        return false;
    }

    if (
        translation->page_size !=
            PAGING_PAGE_SIZE_1G &&
        (translation->pd_entry &
         PAGE_ENTRY_WRITABLE) == 0
    ) {
        return false;
    }

    if (
        translation->page_size ==
            PAGING_PAGE_SIZE_4K &&
        (translation->pt_entry &
         PAGE_ENTRY_WRITABLE) == 0
    ) {
        return false;
    }

    return true;
}

static bool kernel_mapping_translation_user(
    const struct paging_translation *translation)
{
    if (translation == NULL) return false;

    if (
        (translation->pml4_entry &
         PAGE_ENTRY_USER) == 0 ||
        (translation->pdpt_entry &
         PAGE_ENTRY_USER) == 0
    ) {
        return false;
    }

    if (
        translation->page_size !=
            PAGING_PAGE_SIZE_1G &&
        (translation->pd_entry &
         PAGE_ENTRY_USER) == 0
    ) {
        return false;
    }

    if (
        translation->page_size ==
            PAGING_PAGE_SIZE_4K &&
        (translation->pt_entry &
         PAGE_ENTRY_USER) == 0
    ) {
        return false;
    }

    return true;
}

static bool kernel_mapping_translation_executable(
    const struct paging_translation *translation)
{
    if (translation == NULL) return false;

    if (
        (translation->pml4_entry &
         PAGE_ENTRY_NO_EXECUTE) != 0 ||
        (translation->pdpt_entry &
         PAGE_ENTRY_NO_EXECUTE) != 0
    ) {
        return false;
    }

    if (
        translation->page_size !=
            PAGING_PAGE_SIZE_1G &&
        (translation->pd_entry &
         PAGE_ENTRY_NO_EXECUTE) != 0
    ) {
        return false;
    }

    if (
        translation->page_size ==
            PAGING_PAGE_SIZE_4K &&
        (translation->pt_entry &
         PAGE_ENTRY_NO_EXECUTE) != 0
    ) {
        return false;
    }

    return true;
}

// SPDX-License-Identifier: GPL-2.0-only

#include "kernel_mapping.h"

#include "memory.h"
#include "../arch/x86_64/paging.h"

#include <stdbool.h>
#include <stdint.h>

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

static bool kernel_mapping_unmap_range(
    struct paging_address_space *address_space,
    uint64_t start,
    uint64_t end
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

    if (!paging_address_space_create(
        address_space
    )) {
        return false;
    }

    if (!kernel_mapping_map_range(
        address_space,
        (uint64_t) __text_start,
        (uint64_t) __text_end,
        false,
        true
    )) {
        goto fail;
    }

    if (!kernel_mapping_map_range(
        address_space,
        (uint64_t) __rodata_start,
        (uint64_t) __rodata_end,
        false,
        false
    )) {
        goto fail;
    }

    if (!kernel_mapping_map_range(
        address_space,
        (uint64_t) __limine_requests_start,
        (uint64_t) __limine_requests_end,
        false,
        false
    )) {
        goto fail;
    }

    if (!kernel_mapping_map_range(
        address_space,
        (uint64_t) __data_start,
        (uint64_t) __data_end,
        true,
        false
    )) {
        goto fail;
    }

    if (!kernel_mapping_map_range(
        address_space,
        (uint64_t) __bss_start,
        (uint64_t) __bss_end,
        true,
        false
    )) {
        goto fail;
    }

    if (!kernel_mapping_validate_range(
        address_space,
        (uint64_t) __text_start,
        (uint64_t) __text_end,
        false,
        true
    )) {
        goto fail;
    }

    if (!kernel_mapping_validate_range(
        address_space,
        (uint64_t) __rodata_start,
        (uint64_t) __rodata_end,
        false,
        false
    )) {
        goto fail;
    }

    if (!kernel_mapping_validate_range(
        address_space,
        (uint64_t) __limine_requests_start,
        (uint64_t) __limine_requests_end,
        false,
        false
    )) {
        goto fail;
    }

    if (!kernel_mapping_validate_range(
        address_space,
        (uint64_t) __data_start,
        (uint64_t) __data_end,
        true,
        false
    )) {
        goto fail;
    }

    if (!kernel_mapping_validate_range(
        address_space,
        (uint64_t) __bss_start,
        (uint64_t) __bss_end,
        true,
        false
    )) {
        goto fail;
    }

    if (
        address_space->pml4_virtual[
            paging_pml4_index(
                (uint64_t) __text_start
            )
        ] &
        PAGE_ENTRY_USER
    ) {
        goto fail;
    }

    if (
        address_space->pml4_virtual[256] != 0
    ) {
        goto fail;
    }

    return true;

fail:
    kernel_mapping_destroy(address_space);

    return false;
}

bool kernel_mapping_destroy(
    struct paging_address_space *address_space)
{
    if (address_space == NULL) return false;

    if (!kernel_mapping_unmap_range(
        address_space,
        (uint64_t) __bss_start,
        (uint64_t) __bss_end
    )) {
        return false;
    }

    if (!kernel_mapping_unmap_range(
        address_space,
        (uint64_t) __data_start,
        (uint64_t) __data_end
    )) {
        return false;
    }

    if (!kernel_mapping_unmap_range(
        address_space,
        (uint64_t) __limine_requests_start,
        (uint64_t) __limine_requests_end
    )) {
        return false;
    }

    if (!kernel_mapping_unmap_range(
        address_space,
        (uint64_t) __rodata_start,
        (uint64_t) __rodata_end
    )) {
        return false;
    }

    if (!kernel_mapping_unmap_range(
        address_space,
        (uint64_t) __text_start,
        (uint64_t) __text_end
    )) {
        return false;
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

static bool kernel_mapping_unmap_range(
    struct paging_address_space *address_space,
    uint64_t start,
    uint64_t end)
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
        uint64_t physical_address;

        if (!paging_unmap_page(
            address_space,
            virtual_address,
            &physical_address
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

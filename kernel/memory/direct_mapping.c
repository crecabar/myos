// SPDX-License-Identifier: GPL-2.0-only

#include "direct_mapping.h"

#include "memory.h"
#include "../arch/x86_64/paging.h"
#include "../core/panic.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIRECT_MAPPING_TABLE_ENTRY_COUNT 512

#define DIRECT_MAPPING_LEVEL_PT   1U
#define DIRECT_MAPPING_LEVEL_PD   2U
#define DIRECT_MAPPING_LEVEL_PDPT 3U

#define DIRECT_MAPPING_VOLATILE_FLAGS \
    (PAGE_ENTRY_ACCESSED | PAGE_ENTRY_DIRTY)

static bool direct_mapping_installed;
static uint64_t direct_mapping_replaced_entry;

static bool direct_mapping_clone_table(
    uint64_t source_physical,
    unsigned int level,
    uint64_t *destination_physical
);

static bool direct_mapping_release_table(
    uint64_t table_physical,
    unsigned int level
);

static bool direct_mapping_validate(
    const struct paging_address_space *candidate
);

static bool direct_mapping_validate_table(
    uint64_t source_physical,
    uint64_t candidate_physical,
    unsigned int level
);

static uint64_t direct_mapping_owned_leaf_entry(
    uint64_t source_entry
);

static uint64_t direct_mapping_owned_table_entry(
    uint64_t source_entry,
    uint64_t child_physical
);

static uint64_t direct_mapping_stable_entry(
    uint64_t entry
);

bool direct_mapping_install(void)
{
    if (direct_mapping_installed) return false;

    struct paging_address_space candidate;

    if (!direct_mapping_build(
        &candidate
    )) {
        return false;
    }

    struct paging_address_space *kernel_address_space =
        paging_kernel_address_space();

    if (kernel_address_space == NULL) {
        if (!direct_mapping_destroy(
            &candidate
        )) {
            kernel_panic(
                "Unable to destroy uninstalled direct-map candidate"
            );
        }

        return false;
    }

    uint16_t direct_map_pml4_index =
        paging_pml4_index(
            memory_direct_map_base()
        );

    uint64_t current_entry =
        kernel_address_space->pml4_virtual[
            direct_map_pml4_index
        ];

    if (
        (current_entry &
         PAGE_ENTRY_PRESENT) == 0
    ) {
        if (!direct_mapping_destroy(
            &candidate
        )) {
            kernel_panic(
                "Unable to destroy rejected direct-map candidate"
            );
        }

        return false;
    }

    uint64_t replaced_entry;

    if (!paging_address_space_transfer_pml4_branch(
        kernel_address_space,
        &candidate,
        direct_map_pml4_index,
        &replaced_entry
    )) {
        if (!direct_mapping_destroy(
            &candidate
        )) {
            kernel_panic(
                "Unable to destroy failed direct-map candidate"
            );
        }

        return false;
    }

    /*
     * The direct-map PML4 entry has changed beneath the active CR3.
     * Reload CR3 so no cached translations continue to reference the
     * bootloader-owned hierarchy.
     */
    if (!paging_address_space_activate(
        kernel_address_space
    )) {
        kernel_panic(
            "Unable to reload kernel address space after direct-map cutover"
        );
    }

    /*
     * The direct-map branch has moved into the kernel address space.
     * The detached candidate now owns only its temporary PML4 root.
     */
    if (!direct_mapping_destroy(
        &candidate
    )) {
        kernel_panic(
            "Unable to destroy transferred direct-map candidate"
        );
    }

    direct_mapping_replaced_entry =
        replaced_entry;

    direct_mapping_installed = true;

    return true;
}

bool direct_mapping_resolve_physical_range(
    uint64_t physical_address,
    size_t size,
    const void **virtual_address)
{
    if (virtual_address == NULL) {
        return false;
    }

    *virtual_address = NULL;

    if (!direct_mapping_installed) {
        return false;
    }

    if (size == 0) {
        return false;
    }

    uint64_t range_size =
        (uint64_t) size;

    if (
        range_size >
        UINT64_MAX - physical_address
    ) {
        return false;
    }

    uint64_t direct_map_base =
        memory_direct_map_base();

    if (
        physical_address >
        UINT64_MAX - direct_map_base
    ) {
        return false;
    }

    uint64_t virtual_start =
        direct_map_base +
        physical_address;

    /*
     * Check the final byte separately. The half-open physical range
     * may end at UINT64_MAX without requiring us to form end + 1.
     */
    if (
        range_size - 1U >
        UINT64_MAX - virtual_start
    ) {
        return false;
    }

    struct paging_address_space *kernel_space =
        paging_kernel_address_space();

    if (kernel_space == NULL) {
        return false;
    }

    uint64_t remaining =
        range_size;

    uint64_t physical_cursor =
        physical_address;

    uint64_t virtual_cursor =
        virtual_start;

    while (remaining != 0) {
        struct paging_translation translation;

        if (
            !paging_translate_address_space(
                kernel_space,
                virtual_cursor,
                &translation
            )
        ) {
            return false;
        }

        if (
            translation.physical_address !=
            physical_cursor
        ) {
            return false;
        }

        uint64_t page_offset =
            physical_cursor &
            (MEMORY_FRAME_SIZE - 1U);

        uint64_t chunk =
            MEMORY_FRAME_SIZE -
            page_offset;

        if (chunk > remaining) {
            chunk = remaining;
        }

        physical_cursor += chunk;
        virtual_cursor += chunk;
        remaining -= chunk;
    }

    *virtual_address =
        (const void *) virtual_start;

    return true;
}

bool direct_mapping_replaced_branch_entry(
    uint64_t *entry)
{
    if (entry == NULL) return false;
    if (!direct_mapping_installed) return false;

    *entry =
        direct_mapping_replaced_entry;

    return true;
}

bool direct_mapping_build(
    struct paging_address_space *address_space)
{
    if (address_space == NULL) return false;

    struct paging_address_space *active =
        paging_kernel_address_space();

    if (active == NULL) return false;

    struct paging_address_space candidate;

    if (!paging_address_space_create(
        &candidate
    )) {
        return false;
    }

    uint64_t direct_map_base =
        memory_direct_map_base();

    uint16_t pml4_index =
        paging_pml4_index(
            direct_map_base
        );

    uint64_t source_entry =
        active->pml4_virtual[
            pml4_index
        ];

    if (
        (source_entry &
         PAGE_ENTRY_PRESENT) == 0
    ) {
        if (!direct_mapping_destroy(
            &candidate
        )) {
            kernel_panic(
                "Unable to destroy rejected direct-map candidate"
            );
        }

        return false;
    }

    uint64_t source_pdpt_physical =
        paging_entry_address(
            source_entry
        );

    uint64_t candidate_pdpt_physical;

    if (!direct_mapping_clone_table(
        source_pdpt_physical,
        DIRECT_MAPPING_LEVEL_PDPT,
        &candidate_pdpt_physical
    )) {
        if (!direct_mapping_destroy(
            &candidate
        )) {
            kernel_panic(
                "Unable to destroy failed direct-map candidate"
            );
        }

        return false;
    }

    candidate.pml4_virtual[
        pml4_index
    ] =
        direct_mapping_owned_table_entry(
            source_entry,
            candidate_pdpt_physical
        );

    if (!direct_mapping_validate(
        &candidate
    )) {
        if (!direct_mapping_destroy(
            &candidate
        )) {
            kernel_panic(
                "Unable to roll back invalid direct-map candidate"
            );
        }

        return false;
    }

    *address_space = candidate;

    return true;
}

bool direct_mapping_destroy(
    struct paging_address_space *address_space)
{
    if (address_space == NULL) return false;
    if (address_space->pml4_virtual == NULL) return false;
    if (address_space->kernel_half_shared) return false;

    uint16_t direct_map_pml4_index =
        paging_pml4_index(
            memory_direct_map_base()
        );

    for (
        size_t index = 0;
        index < DIRECT_MAPPING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        if (index == direct_map_pml4_index) {
            continue;
        }

        if (
            (address_space->pml4_virtual[index] &
             PAGE_ENTRY_PRESENT) != 0
        ) {
            return false;
        }
    }

    uint64_t direct_map_entry =
        address_space->pml4_virtual[
            direct_map_pml4_index
        ];

    if (
        (direct_map_entry &
         PAGE_ENTRY_PRESENT) != 0
    ) {
        uint64_t pdpt_physical =
            paging_entry_address(
                direct_map_entry
            );

        if (!direct_mapping_release_table(
            pdpt_physical,
            DIRECT_MAPPING_LEVEL_PDPT
        )) {
            return false;
        }

        address_space->pml4_virtual[
            direct_map_pml4_index
        ] = 0;
    }

    return paging_address_space_destroy(
        address_space
    );
}

static bool direct_mapping_clone_table(
    uint64_t source_physical,
    unsigned int level,
    uint64_t *destination_physical)
{
    if (destination_physical == NULL) {
        return false;
    }

    if (
        level < DIRECT_MAPPING_LEVEL_PT ||
        level > DIRECT_MAPPING_LEVEL_PDPT
    ) {
        return false;
    }

    uint64_t cloned_physical;
    uint64_t *cloned_table;

    if (!paging_create_empty_table(
        &cloned_physical,
        &cloned_table
    )) {
        return false;
    }

    const uint64_t *source_table =
        memory_physical_to_virtual(
            source_physical
        );

    for (
        size_t index = 0;
        index < DIRECT_MAPPING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        uint64_t source_entry =
            source_table[index];

        if (
            (source_entry &
             PAGE_ENTRY_PRESENT) == 0
        ) {
            continue;
        }

        if (level == DIRECT_MAPPING_LEVEL_PT) {
            cloned_table[index] =
                direct_mapping_owned_leaf_entry(
                    source_entry
                );

            continue;
        }

        if (
            (source_entry &
             PAGE_ENTRY_HUGE) != 0
        ) {
            cloned_table[index] =
                direct_mapping_owned_leaf_entry(
                    source_entry
                );

            continue;
        }

        uint64_t source_child_physical =
            paging_entry_address(
                source_entry
            );

        uint64_t cloned_child_physical;

        if (!direct_mapping_clone_table(
            source_child_physical,
            level - 1,
            &cloned_child_physical
        )) {
            if (!direct_mapping_release_table(
                cloned_physical,
                level
            )) {
                kernel_panic(
                    "Unable to roll back partial direct-map clone"
                );
            }

            return false;
        }

        cloned_table[index] =
            direct_mapping_owned_table_entry(
                source_entry,
                cloned_child_physical
            );
    }

    *destination_physical =
        cloned_physical;

    return true;
}

static bool direct_mapping_release_table(
    uint64_t table_physical,
    unsigned int level)
{
    if (
        level < DIRECT_MAPPING_LEVEL_PT ||
        level > DIRECT_MAPPING_LEVEL_PDPT
    ) {
        return false;
    }

    uint64_t *table =
        memory_physical_to_virtual(
            table_physical
        );

    for (
        size_t index = 0;
        index < DIRECT_MAPPING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        uint64_t entry =
            table[index];

        if (
            (entry &
             PAGE_ENTRY_PRESENT) == 0
        ) {
            continue;
        }

        if (level == DIRECT_MAPPING_LEVEL_PT) {
            table[index] = 0;
            continue;
        }

        if (
            (entry &
             PAGE_ENTRY_HUGE) != 0
        ) {
            table[index] = 0;
            continue;
        }

        uint64_t child_physical =
            paging_entry_address(
                entry
            );

        if (!direct_mapping_release_table(
            child_physical,
            level - 1
        )) {
            return false;
        }

        table[index] = 0;
    }

    return physical_free_frame(
        table_physical
    );
}

static bool direct_mapping_validate(
    const struct paging_address_space *candidate)
{
    if (candidate == NULL) return false;
    if (candidate->pml4_virtual == NULL) return false;

    struct paging_address_space *active =
        paging_kernel_address_space();

    if (active == NULL) return false;

    uint16_t pml4_index =
        paging_pml4_index(
            memory_direct_map_base()
        );

    for (
        size_t index = 0;
        index < DIRECT_MAPPING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        bool present =
            (
                candidate->pml4_virtual[index] &
                PAGE_ENTRY_PRESENT
            ) != 0;

        if (index == pml4_index) {
            if (!present) return false;
            continue;
        }

        if (present) return false;
    }

    uint64_t source_entry =
        active->pml4_virtual[
            pml4_index
        ];

    uint64_t candidate_entry =
        candidate->pml4_virtual[
            pml4_index
        ];

    if (
        (source_entry &
         PAGE_ENTRY_PRESENT) == 0 ||
        (candidate_entry &
         PAGE_ENTRY_PRESENT) == 0
    ) {
        return false;
    }

    uint64_t source_pdpt_physical =
        paging_entry_address(
            source_entry
        );

    uint64_t candidate_pdpt_physical =
        paging_entry_address(
            candidate_entry
        );

    if (
        source_pdpt_physical ==
        candidate_pdpt_physical
    ) {
        return false;
    }

    uint64_t expected_candidate_entry =
        direct_mapping_owned_table_entry(
            source_entry,
            candidate_pdpt_physical
        );

    if (
        candidate_entry !=
        expected_candidate_entry
    ) {
        return false;
    }

    return direct_mapping_validate_table(
        source_pdpt_physical,
        candidate_pdpt_physical,
        DIRECT_MAPPING_LEVEL_PDPT
    );
}

static bool direct_mapping_validate_table(
    uint64_t source_physical,
    uint64_t candidate_physical,
    unsigned int level)
{
    if (
        level < DIRECT_MAPPING_LEVEL_PT ||
        level > DIRECT_MAPPING_LEVEL_PDPT
    ) {
        return false;
    }

    if (
        source_physical ==
        candidate_physical
    ) {
        return false;
    }

    const uint64_t *source =
        memory_physical_to_virtual(
            source_physical
        );

    const uint64_t *candidate =
        memory_physical_to_virtual(
            candidate_physical
        );

    for (
        size_t index = 0;
        index < DIRECT_MAPPING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        uint64_t source_entry =
            source[index];

        uint64_t candidate_entry =
            candidate[index];

        bool source_present =
            (
                source_entry &
                PAGE_ENTRY_PRESENT
            ) != 0;

        bool candidate_present =
            (
                candidate_entry &
                PAGE_ENTRY_PRESENT
            ) != 0;

        if (
            source_present !=
            candidate_present
        ) {
            return false;
        }

        if (!source_present) {
            if (candidate_entry != 0) {
                return false;
            }

            continue;
        }

        if (level == DIRECT_MAPPING_LEVEL_PT) {
            if (
                candidate_entry !=
                direct_mapping_owned_leaf_entry(
                    source_entry
                )
            ) {
                return false;
            }

            continue;
        }

        bool source_huge =
            (
                source_entry &
                PAGE_ENTRY_HUGE
            ) != 0;

        bool candidate_huge =
            (
                candidate_entry &
                PAGE_ENTRY_HUGE
            ) != 0;

        if (
            source_huge !=
            candidate_huge
        ) {
            return false;
        }

        if (source_huge) {
            if (
                candidate_entry !=
                direct_mapping_owned_leaf_entry(
                    source_entry
                )
            ) {
                return false;
            }

            continue;
        }

        uint64_t source_child_physical =
            paging_entry_address(
                source_entry
            );

        uint64_t candidate_child_physical =
            paging_entry_address(
                candidate_entry
            );

        if (
            source_child_physical ==
            candidate_child_physical
        ) {
            return false;
        }

        if (
            candidate_entry !=
            direct_mapping_owned_table_entry(
                source_entry,
                candidate_child_physical
            )
        ) {
            return false;
        }

        if (!direct_mapping_validate_table(
            source_child_physical,
            candidate_child_physical,
            level - 1
        )) {
            return false;
        }
    }

    return true;
}

static uint64_t direct_mapping_owned_leaf_entry(
    uint64_t source_entry)
{
    uint64_t entry =
        direct_mapping_stable_entry(
            source_entry
        );

    return
        (entry &
         ~PAGE_ENTRY_USER) |
        PAGE_ENTRY_NO_EXECUTE;
}

static uint64_t direct_mapping_owned_table_entry(
    uint64_t source_entry,
    uint64_t child_physical)
{
    uint64_t stable_entry =
        direct_mapping_stable_entry(
            source_entry
        );

    uint64_t flags =
        stable_entry &
        ~PAGE_ADDRESS_MASK_4K;

    flags &=
        ~PAGE_ENTRY_USER;

    flags |=
        PAGE_ENTRY_NO_EXECUTE;

    return
        (child_physical &
         PAGE_ADDRESS_MASK_4K) |
        flags;
}

static uint64_t direct_mapping_stable_entry(uint64_t entry)
{
    return
        entry &
        ~DIRECT_MAPPING_VOLATILE_FLAGS;
}

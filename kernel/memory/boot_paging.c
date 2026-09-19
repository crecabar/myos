// SPDX-License-Identifier: GPL-2.0-only

#include "boot_paging.h"

#include "memory.h"
#include "../arch/x86_64/paging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOT_PAGING_TABLE_ENTRY_COUNT 512

#define BOOT_PAGING_LEVEL_PT   1U
#define BOOT_PAGING_LEVEL_PD   2U
#define BOOT_PAGING_LEVEL_PDPT 3U
#define BOOT_PAGING_LEVEL_PML4 4U

struct boot_paging_inventory_state {
    uint64_t boot_pml4_physical;

    const struct paging_address_space *active;

    struct boot_paging_inventory *inventory;
};

static bool boot_paging_walk_table(
    uint64_t table_physical,
    unsigned int level,
    struct boot_paging_inventory_state *state
);

static bool boot_paging_note_table(
    uint64_t table_physical,
    unsigned int level,
    struct boot_paging_inventory_state *state
);

static bool boot_paging_entry_points_to_table(
    uint64_t entry,
    unsigned int level
);

static bool boot_paging_tree_contains_table(
    uint64_t table_physical,
    unsigned int level,
    uint64_t target_physical
);

static uint64_t boot_paging_tree_reference_count(
    uint64_t table_physical,
    unsigned int level,
    uint64_t target_physical
);

bool boot_paging_inventory_collect(
    struct boot_paging_inventory *inventory)
{
    if (inventory == NULL) return false;

    inventory->pml4_frames = 0;
    inventory->pdpt_frames = 0;
    inventory->pd_frames = 0;
    inventory->pt_frames = 0;
    inventory->duplicate_table_references = 0;
    inventory->active_table_overlaps = 0;
    inventory->non_reclaimable_table_frames = 0;

    uint64_t boot_pml4_physical;

    if (!paging_boot_pml4_physical(
        &boot_pml4_physical
    )) {
        return false;
    }

    struct paging_address_space *active =
        paging_kernel_address_space();

    if (active == NULL) return false;

    struct boot_paging_inventory_state state = {
        .boot_pml4_physical =
            boot_pml4_physical,

        .active =
            active,

        .inventory =
            inventory,
    };

    if (!boot_paging_note_table(
        boot_pml4_physical,
        BOOT_PAGING_LEVEL_PML4,
        &state
    )) {
        return false;
    }

    return boot_paging_walk_table(
        boot_pml4_physical,
        BOOT_PAGING_LEVEL_PML4,
        &state
    );
}

static bool boot_paging_walk_table(
    uint64_t table_physical,
    unsigned int level,
    struct boot_paging_inventory_state *state)
{
    if (state == NULL) return false;

    if (
        level < BOOT_PAGING_LEVEL_PT ||
        level > BOOT_PAGING_LEVEL_PML4
    ) {
        return false;
    }

    const uint64_t *table =
        memory_physical_to_virtual(
            table_physical
        );

    for (
        size_t index = 0;
        index < BOOT_PAGING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        uint64_t entry =
            table[index];

        if (!boot_paging_entry_points_to_table(
            entry,
            level
        )) {
            continue;
        }

        uint64_t child_physical =
            paging_entry_address(
                entry
            );

        if (!boot_paging_note_table(
            child_physical,
            level - 1,
            state
        )) {
            return false;
        }

        if (!boot_paging_walk_table(
            child_physical,
            level - 1,
            state
        )) {
            return false;
        }
    }

    return true;
}

static bool boot_paging_note_table(
    uint64_t table_physical,
    unsigned int level,
    struct boot_paging_inventory_state *state)
{
    if (state == NULL) return false;
    if (state->inventory == NULL) return false;

    if (
        (table_physical %
         MEMORY_FRAME_SIZE) != 0
    ) {
        return false;
    }

    switch (level) {
        case BOOT_PAGING_LEVEL_PML4:
            ++state->inventory->pml4_frames;
            break;

        case BOOT_PAGING_LEVEL_PDPT:
            ++state->inventory->pdpt_frames;
            break;

        case BOOT_PAGING_LEVEL_PD:
            ++state->inventory->pd_frames;
            break;

        case BOOT_PAGING_LEVEL_PT:
            ++state->inventory->pt_frames;
            break;

        default:
            return false;
    }

    if (
        !memory_physical_frame_is_bootloader_reclaimable(
            table_physical
        )
    ) {
        ++state
            ->inventory
            ->non_reclaimable_table_frames;
    }

    if (
        boot_paging_tree_contains_table(
            state->active->pml4_physical,
            BOOT_PAGING_LEVEL_PML4,
            table_physical
        )
    ) {
        ++state
            ->inventory
            ->active_table_overlaps;
    }

    uint64_t reference_count =
        boot_paging_tree_reference_count(
            state->boot_pml4_physical,
            BOOT_PAGING_LEVEL_PML4,
            table_physical
        );

    if (
        level ==
        BOOT_PAGING_LEVEL_PML4
    ) {
        /*
         * The boot PML4 has no parent reference. A structural reference back
         * to the root would therefore indicate an alias or cycle.
         */
        if (reference_count != 0) {
            ++state
                ->inventory
                ->duplicate_table_references;
        }
    } else if (reference_count != 1) {
        ++state
            ->inventory
            ->duplicate_table_references;
    }

    return true;
}

static bool boot_paging_entry_points_to_table(
    uint64_t entry,
    unsigned int level)
{
    if (
        (entry &
         PAGE_ENTRY_PRESENT) == 0
    ) {
        return false;
    }

    if (
        level ==
        BOOT_PAGING_LEVEL_PT
    ) {
        return false;
    }

    if (
        (
            level ==
                BOOT_PAGING_LEVEL_PDPT ||
            level ==
                BOOT_PAGING_LEVEL_PD
        ) &&
        (entry &
         PAGE_ENTRY_HUGE) != 0
    ) {
        return false;
    }

    return true;
}

static bool boot_paging_tree_contains_table(
    uint64_t table_physical,
    unsigned int level,
    uint64_t target_physical)
{
    if (
        table_physical ==
        target_physical
    ) {
        return true;
    }

    if (
        level <=
        BOOT_PAGING_LEVEL_PT
    ) {
        return false;
    }

    const uint64_t *table =
        memory_physical_to_virtual(
            table_physical
        );

    for (
        size_t index = 0;
        index < BOOT_PAGING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        uint64_t entry =
            table[index];

        if (!boot_paging_entry_points_to_table(
            entry,
            level
        )) {
            continue;
        }

        uint64_t child_physical =
            paging_entry_address(
                entry
            );

        if (boot_paging_tree_contains_table(
            child_physical,
            level - 1,
            target_physical
        )) {
            return true;
        }
    }

    return false;
}

static uint64_t boot_paging_tree_reference_count(
    uint64_t table_physical,
    unsigned int level,
    uint64_t target_physical)
{
    if (
        level <=
        BOOT_PAGING_LEVEL_PT
    ) {
        return 0;
    }

    const uint64_t *table =
        memory_physical_to_virtual(
            table_physical
        );

    uint64_t references = 0;

    for (
        size_t index = 0;
        index < BOOT_PAGING_TABLE_ENTRY_COUNT;
        ++index
    ) {
        uint64_t entry =
            table[index];

        if (!boot_paging_entry_points_to_table(
            entry,
            level
        )) {
            continue;
        }

        uint64_t child_physical =
            paging_entry_address(
                entry
            );

        if (
            child_physical ==
            target_physical
        ) {
            ++references;
        }

        references +=
            boot_paging_tree_reference_count(
                child_physical,
                level - 1,
                target_physical
            );
    }

    return references;
}

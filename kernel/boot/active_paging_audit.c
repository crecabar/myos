// SPDX-License-Identifier: GPL-2.0-only

#include "active_paging_audit.h"

#include "../arch/x86_64/paging.h"
#include "../memory/memory.h"

#include <stddef.h>
#include <stdint.h>

#define ACTIVE_PAGING_ENTRY_COUNT 512U
#define ACTIVE_PAGING_LEVEL_PT    1U
#define ACTIVE_PAGING_LEVEL_PD    2U
#define ACTIVE_PAGING_LEVEL_PDPT  3U
#define ACTIVE_PAGING_LEVEL_PML4  4U

/* Helpers and private functions */
static bool active_paging_frame_is_safe(
    const struct boot_info *boot_info,
    uint64_t physical_address
);

static bool active_paging_audit_table(
    const struct boot_info *boot_info,
    uint64_t table_physical,
    unsigned int level,
    uint64_t ancestors[ACTIVE_PAGING_LEVEL_PML4],
    struct boot_active_paging_audit_report *report
);
/* END HELPERS */

bool boot_active_paging_audit(
    const struct boot_info *boot_info,
    struct boot_active_paging_audit_report *report)
{
    if (
        boot_info == NULL ||
        report == NULL
    ) {
        return false;
    }

    if (
        boot_info->memory_region_count == 0 ||
        boot_info->memory_region_count >
            BOOT_MEMORY_REGION_MAX
    ) {
        return false;
    }

    struct paging_address_space *active =
        paging_kernel_address_space();

    if (active == NULL) {
        return false;
    }

    uint64_t active_cr3 =
        paging_read_cr3() &
        PAGE_ADDRESS_MASK_4K;

    if (
        active_cr3 != active->pml4_physical ||
        active->pml4_virtual == NULL
    ) {
        return false;
    }

    report->pml4_tables = 0;
    report->pdpt_tables = 0;
    report->pd_tables = 0;
    report->pt_tables = 0;
    report->large_1g_mappings = 0;
    report->large_2m_mappings = 0;
    report->free_frames = 0;

    uint64_t free_before =
        physical_free_frame_count();

    uint64_t ancestors[
        ACTIVE_PAGING_LEVEL_PML4
    ] = { 0 };

    if (
        !active_paging_audit_table(
            boot_info,
            active_cr3,
            ACTIVE_PAGING_LEVEL_PML4,
            ancestors,
            report
        )
    ) {
        return false;
    }

    uint64_t free_after =
        physical_free_frame_count();

    if (free_after != free_before) {
        return false;
    }

    report->free_frames = free_after;

    return true;
}

static bool active_paging_frame_is_safe(
    const struct boot_info *boot_info,
    uint64_t physical_address)
{
    if (
        (physical_address &
         (MEMORY_FRAME_SIZE - 1)) != 0
    ) {
        return false;
    }

    uint64_t managed_limit =
        memory_managed_physical_limit();

    if (
        physical_address >= managed_limit ||
        managed_limit - physical_address <
            MEMORY_FRAME_SIZE
    ) {
        return false;
    }

    /*
     * Reject any overlap with a bootloader-reclaimable region,
     * including overlaps with non-page-aligned region boundaries.
     */
    for (
        size_t index = 0;
        index < boot_info->memory_region_count;
        ++index
    ) {
        const struct memory_region *region =
            &boot_info->memory_regions[index];

        if (
            region->length == 0 ||
            region->length >
                UINT64_MAX - region->base
        ) {
            return false;
        }

        if (
            region->type !=
            MEMORY_REGION_BOOTLOADER_RECLAIMABLE
        ) {
            continue;
        }

        uint64_t region_end =
            region->base + region->length;

        uint64_t frame_end =
            physical_address +
            MEMORY_FRAME_SIZE;

        if (
            physical_address < region_end &&
            region->base < frame_end
        ) {
            return false;
        }
    }

    if (
        memory_bootloader_frame_reclaim_pending(
            physical_address
        )
    ) {
        return false;
    }

    return true;
}

static bool active_paging_audit_table(
    const struct boot_info *boot_info,
    uint64_t table_physical,
    unsigned int level,
    uint64_t ancestors[ACTIVE_PAGING_LEVEL_PML4],
    struct boot_active_paging_audit_report *report)
{
    if (
        level < ACTIVE_PAGING_LEVEL_PT ||
        level > ACTIVE_PAGING_LEVEL_PML4
    ) {
        return false;
    }

    /*
     * A child table must not point back to a table already present
     * on its current ancestry path.
     */
    for (
        unsigned int ancestor_level = level + 1;
        ancestor_level <= ACTIVE_PAGING_LEVEL_PML4;
        ++ancestor_level
    ) {
        if (
            ancestors[ancestor_level - 1] ==
            table_physical
        ) {
            return false;
        }
    }

    if (
        !active_paging_frame_is_safe(
            boot_info,
            table_physical
        )
    ) {
        return false;
    }

    ancestors[level - 1] =
        table_physical;

    switch (level) {
        case ACTIVE_PAGING_LEVEL_PML4:
            ++report->pml4_tables;
            break;

        case ACTIVE_PAGING_LEVEL_PDPT:
            ++report->pdpt_tables;
            break;

        case ACTIVE_PAGING_LEVEL_PD:
            ++report->pd_tables;
            break;

        case ACTIVE_PAGING_LEVEL_PT:
            ++report->pt_tables;
            break;

        default:
            return false;
    }

    /*
     * The owned direct map is already installed. Inspect only
     * the paging-structure frame, never a leaf-mapped data frame.
     */
    const uint64_t *table =
        memory_physical_to_virtual(
            table_physical
        );

    for (
        size_t index = 0;
        index < ACTIVE_PAGING_ENTRY_COUNT;
        ++index
    ) {
        uint64_t entry =
            table[index];

        if (
            (entry & PAGE_ENTRY_PRESENT) == 0
        ) {
            continue;
        }

        /*
         * Huge mappings terminate at PDPT (1 GiB) or PD (2 MiB).
         * At PT level, bit 7 is PAT, not a huge-page indicator.
         */
        if (
            (entry & PAGE_ENTRY_HUGE) != 0
        ) {
            if (
                level ==
                ACTIVE_PAGING_LEVEL_PDPT
            ) {
                ++report->large_1g_mappings;
                continue;
            }

            if (
                level ==
                ACTIVE_PAGING_LEVEL_PD
            ) {
                ++report->large_2m_mappings;
                continue;
            }

            if (
                level ==
                ACTIVE_PAGING_LEVEL_PML4
            ) {
                return false;
            }
        }

        if (
            level ==
            ACTIVE_PAGING_LEVEL_PT
        ) {
            /*
             * This entry maps data, not another paging structure.
             */
            continue;
        }

        uint64_t child_physical =
            paging_entry_address(
                entry
            );

        if (
            !active_paging_audit_table(
                boot_info,
                child_physical,
                level - 1,
                ancestors,
                report
            )
        ) {
            return false;
        }
    }

    return true;
}

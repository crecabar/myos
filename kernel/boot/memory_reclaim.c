// SPDX-License-Identifier: GPL-2.0-only

#include "memory_reclaim.h"

#include "../arch/x86_64/paging.h"
#include "../core/panic.h"
#include "../memory/memory.h"
#include "active_paging_audit.h"
#include "physical_range_audit.h"
#include "reclaim_preflight.h"

#include <stddef.h>
#include <stdint.h>

#define BOOT_RECLAIM_PML4_ENTRY_COUNT 512U
#define BOOT_RECLAIM_MIN_ADDRESS 0x100000ULL

extern char __kernel_start[];

static bool general_reclamation_complete;

/* Helpers and private functions */
static bool boot_memory_reclaim_kernel_branches_only(void);
/* END HELPERS */

bool boot_memory_reclaim_remaining(
    const struct boot_info *boot_info,
    struct boot_memory_reclaim_result *result)
{
    if (
        boot_info == NULL ||
        result == NULL ||
        general_reclamation_complete
    ) {
        return false;
    }

    /*
     * Re-run every prerequisite at the actual reclamation point.
     * Do not rely only on reports collected earlier in kernel_main().
     */
    struct boot_reclaim_preflight_report preflight;

    if (
        !boot_reclaim_preflight(
            boot_info,
            &preflight
        )
    ) {
        return false;
    }

    struct boot_physical_range_audit_report ranges;

    if (
        !boot_physical_range_audit(
            boot_info,
            &preflight,
            &ranges
        )
    ) {
        return false;
    }

    struct boot_active_paging_audit_report paging;

    if (
        !boot_active_paging_audit(
            boot_info,
            &paging
        )
    ) {
        return false;
    }

    if (
        paging.pml4_tables != 1 ||
        !boot_memory_reclaim_kernel_branches_only()
    ) {
        return false;
    }

    uint64_t free_before =
        physical_free_frame_count();

    if (
        free_before != preflight.free_frames ||
        free_before != ranges.free_frames ||
        free_before != paging.free_frames ||
        ranges.pending_frames !=
            preflight.inventory.pending_frames
    ) {
        return false;
    }

    uint64_t expected_reclaimed =
        preflight.inventory.pending_frames;

    if (
        expected_reclaimed >
        UINT64_MAX - free_before
    ) {
        return false;
    }

    /*
     * The validated boot memory map is kernel-owned. Only complete,
     * frame-aligned portions of bootloader-reclaimable regions are
     * eligible. Retain the allocator's existing first-MiB exclusion.
     */
    uint64_t reclaimed = 0;
    uint64_t first_reclaimed = 0;

    for (
        size_t index = 0;
        index < boot_info->memory_region_count;
        ++index
    ) {
        const struct memory_region *region =
            &boot_info->memory_regions[index];

        if (
            region->type !=
            MEMORY_REGION_BOOTLOADER_RECLAIMABLE
        ) {
            continue;
        }

        /*
         * The range audit checked region lengths and non-overlap.
         * Retain an explicit overflow guard at the transfer point.
         */
        if (
            region->length == 0 ||
            region->length >
                UINT64_MAX - region->base
        ) {
            kernel_panic(
                "Bootloader reclaim region changed after audit"
            );
        }

        uint64_t region_end =
            region->base + region->length;

        uint64_t first_frame =
            region->base;

        uint64_t remainder =
            first_frame &
            (MEMORY_FRAME_SIZE - 1);

        if (remainder != 0) {
            uint64_t adjustment =
                MEMORY_FRAME_SIZE - remainder;

            if (
                first_frame >
                UINT64_MAX - adjustment
            ) {
                kernel_panic(
                    "Bootloader reclaim frame alignment overflow"
                );
            }

            first_frame += adjustment;
        }

        if (
            first_frame <
            BOOT_RECLAIM_MIN_ADDRESS
        ) {
            first_frame =
                BOOT_RECLAIM_MIN_ADDRESS;
        }

        uint64_t last_frame =
            region_end &
            ~(MEMORY_FRAME_SIZE - 1);

        for (
            uint64_t physical_address =
                first_frame;
            physical_address < last_frame;
            physical_address += MEMORY_FRAME_SIZE
        ) {
            /*
             * Frames transferred by #30 are no longer pending.
             * Do not try to reclaim them again.
             */
            if (
                !memory_bootloader_frame_reclaim_pending(
                    physical_address
                )
            ) {
                continue;
            }

            if (
                !memory_bootloader_frame_reclaim(
                    physical_address
                )
            ) {
                kernel_panic(
                    "General bootloader frame transfer failed"
                );
            }

            if (reclaimed == 0) {
                first_reclaimed =
                    physical_address;
            }

            ++reclaimed;
        }
    }

    /*
     * A failure from this point cannot be handled by returning false:
     * some frames may already be available to the allocator.
     */
    if (reclaimed != expected_reclaimed) {
        kernel_panic(
            "General bootloader reclaim count differs from preflight"
        );
    }

    uint64_t free_after =
        physical_free_frame_count();

    if (
        free_after !=
        free_before + reclaimed
    ) {
        kernel_panic(
            "General bootloader reclaim free-frame count mismatch"
        );
    }

    struct memory_bootloader_frame_inventory after;

    if (
        !memory_bootloader_frame_inventory_collect(
            &after
        )
    ) {
        kernel_panic(
            "Unable to collect post-reclaim inventory"
        );
    }

    if (
        after.eligible_frames !=
            preflight.inventory.eligible_frames ||
        after.pending_frames != 0 ||
        after.transferred_free_frames !=
            preflight.inventory.transferred_free_frames +
                reclaimed ||
        after.transferred_used_frames !=
            preflight.inventory.transferred_used_frames
    ) {
        kernel_panic(
            "General bootloader reclaim ownership mismatch"
        );
    }

    result->reclaimed_frames =
        reclaimed;

    result->first_reclaimed_frame =
        first_reclaimed;

    result->free_before =
        free_before;

    result->free_after =
        free_after;

    general_reclamation_complete = true;

    return true;
}

static bool boot_memory_reclaim_kernel_branches_only(void)
{
    struct paging_address_space *active =
        paging_kernel_address_space();

    if (
        active == NULL ||
        active->pml4_virtual == NULL
    ) {
        return false;
    }

    uint16_t direct_map_index =
        paging_pml4_index(
            memory_direct_map_base()
        );

    uint16_t kernel_index =
        paging_pml4_index(
            (uint64_t) __kernel_start
        );

    if (
        direct_map_index == kernel_index ||
        direct_map_index < 256 ||
        kernel_index < 256
    ) {
        return false;
    }

    const uint64_t *pml4 =
        active->pml4_virtual;

    if (
        (pml4[direct_map_index] &
         PAGE_ENTRY_PRESENT) == 0 ||
        (pml4[kernel_index] &
         PAGE_ENTRY_PRESENT) == 0
    ) {
        return false;
    }

    /*
     * At this early cutover, MyOS expects only its own kernel and
     * direct-map branches. Reject any unexpected additional branch
     * rather than assuming that its backing memory is safe to reuse.
     */
    for (
        size_t index = 0;
        index < BOOT_RECLAIM_PML4_ENTRY_COUNT;
        ++index
    ) {
        if (
            index == direct_map_index ||
            index == kernel_index
        ) {
            continue;
        }

        if (
            (pml4[index] &
             PAGE_ENTRY_PRESENT) != 0
        ) {
            return false;
        }
    }

    return true;
}

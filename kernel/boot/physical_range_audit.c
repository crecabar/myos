// SPDX-License-Identifier: GPL-2.0-only

#include "physical_range_audit.h"

#include "../arch/x86_64/paging.h"
#include "../memory/memory.h"

#include <stddef.h>
#include <stdint.h>

#define BOOT_FRAMEBUFFER_WINDOW_SIZE 0x20000000ULL

extern char __kernel_start[];
extern char __kernel_end[];

/* Helpers and private functions */
static bool boot_physical_range_check_mapping(
    uint64_t virtual_address
);

static bool boot_physical_range_check_kernel_image(
    struct boot_physical_range_audit_report *report
);

static bool boot_physical_range_check_framebuffer(
    const struct framebuffer *framebuffer,
    struct boot_physical_range_audit_report *report
);
/* END HELPERS */

bool boot_physical_range_audit(
    const struct boot_info *boot_info,
    const struct boot_reclaim_preflight_report *preflight,
    struct boot_physical_range_audit_report *report)
{
    if (
        boot_info == NULL ||
        preflight == NULL ||
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

    report->reclaimable_regions = 0;
    report->eligible_frames = 0;
    report->pending_frames = 0;
    report->kernel_image_pages_checked = 0;
    report->framebuffer_pages_checked = 0;
    report->free_frames = 0;

    uint64_t free_before =
        physical_free_frame_count();

    if (free_before != preflight->free_frames) {
        return false;
    }

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

        uint64_t region_end =
            region->base + region->length;

        /*
         * A frame must not receive contradictory classifications from
         * overlapping boot memory-map entries.
         */
        for (
            size_t other_index = index + 1;
            other_index <
                boot_info->memory_region_count;
            ++other_index
        ) {
            const struct memory_region *other =
                &boot_info->memory_regions[other_index];

            if (
                other->length == 0 ||
                other->length >
                    UINT64_MAX - other->base
            ) {
                return false;
            }

            uint64_t other_end =
                other->base + other->length;

            if (
                region->base < other_end &&
                other->base < region_end
            ) {
                return false;
            }
        }

        if (
            region->type !=
            MEMORY_REGION_BOOTLOADER_RECLAIMABLE
        ) {
            continue;
        }

        ++report->reclaimable_regions;

        uint64_t first_frame_address =
            region->base;

        uint64_t misalignment =
            first_frame_address &
            (MEMORY_FRAME_SIZE - 1);

        if (misalignment != 0) {
            uint64_t adjustment =
                MEMORY_FRAME_SIZE - misalignment;

            if (
                first_frame_address >
                UINT64_MAX - adjustment
            ) {
                return false;
            }

            first_frame_address += adjustment;
        }

        /*
         * Preserve the allocator's existing first-MiB exclusion.
         */
        if (
            first_frame_address <
            0x100000ULL
        ) {
            first_frame_address = 0x100000ULL;
        }

        uint64_t last_frame_address =
            region_end &
            ~(MEMORY_FRAME_SIZE - 1);

        for (
            uint64_t physical_address =
                first_frame_address;
            physical_address <
                last_frame_address;
            physical_address += MEMORY_FRAME_SIZE
        ) {
            ++report->eligible_frames;

            if (
                memory_bootloader_frame_reclaim_pending(
                    physical_address
                )
            ) {
                ++report->pending_frames;
            }
        }
    }

    /*
     * The independently collected range totals must agree with the
     * physical allocator's existing ownership inventory.
     */
    if (
        report->eligible_frames !=
            preflight->inventory.eligible_frames ||
        report->pending_frames !=
            preflight->inventory.pending_frames
    ) {
        return false;
    }

    struct paging_address_space *active =
        paging_kernel_address_space();

    if (active == NULL) {
        return false;
    }

    uint64_t active_root =
        paging_read_cr3() &
        PAGE_ADDRESS_MASK_4K;

    if (
        active_root != active->pml4_physical ||
        memory_physical_frame_is_bootloader_reclaimable(
            active_root
        )
    ) {
        return false;
    }

    if (
        !boot_physical_range_check_kernel_image(
            report
        ) ||
        !boot_physical_range_check_framebuffer(
            &boot_info->framebuffer,
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

static bool boot_physical_range_check_mapping(
    uint64_t virtual_address)
{
    struct paging_translation translation;

    if (
        !paging_translate(
            virtual_address,
            &translation
        )
    ) {
        return false;
    }

    uint64_t physical_frame =
        translation.physical_address &
        PAGE_ADDRESS_MASK_4K;

    /*
     * Unlike the direct map, these mappings represent live kernel
     * resources. Their backing must not belong to a bootloader-memory
     * candidate region.
     */
    return
        !memory_physical_frame_is_bootloader_reclaimable(
            physical_frame
        );
}

static bool boot_physical_range_check_kernel_image(
    struct boot_physical_range_audit_report *report)
{
    uint64_t image_start =
        (uint64_t) __kernel_start;

    uint64_t image_end =
        (uint64_t) __kernel_end;

    if (
        image_end <= image_start ||
        (image_start &
         (MEMORY_FRAME_SIZE - 1)) != 0 ||
        (image_end &
         (MEMORY_FRAME_SIZE - 1)) != 0
    ) {
        return false;
    }

    for (
        uint64_t virtual_address = image_start;
        virtual_address < image_end;
        virtual_address += MEMORY_FRAME_SIZE
    ) {
        if (
            !boot_physical_range_check_mapping(
                virtual_address
            )
        ) {
            return false;
        }

        ++report->kernel_image_pages_checked;
    }

    return true;
}

static bool boot_physical_range_check_framebuffer(
    const struct framebuffer *framebuffer,
    struct boot_physical_range_audit_report *report)
{
    if (
        framebuffer == NULL ||
        framebuffer->address == NULL ||
        framebuffer->pitch == 0 ||
        framebuffer->height == 0
    ) {
        return false;
    }

    if (
        framebuffer->pitch >
        UINT64_MAX / framebuffer->height
    ) {
        return false;
    }

    uint64_t framebuffer_size =
        framebuffer->pitch * framebuffer->height;

    uint64_t framebuffer_start =
        (uint64_t) framebuffer->address;

    uint64_t first_page =
        framebuffer_start &
        ~(MEMORY_FRAME_SIZE - 1);

    uint64_t page_offset =
        framebuffer_start &
        (MEMORY_FRAME_SIZE - 1);

    if (
        framebuffer_size == 0 ||
        framebuffer_size >
            UINT64_MAX - page_offset
    ) {
        return false;
    }

    uint64_t required_size =
        framebuffer_size + page_offset;

    if (
        required_size >
        UINT64_MAX - (MEMORY_FRAME_SIZE - 1)
    ) {
        return false;
    }

    uint64_t mapped_size =
        (
            required_size +
            MEMORY_FRAME_SIZE - 1
        ) &
        ~(MEMORY_FRAME_SIZE - 1);

    if (
        mapped_size == 0 ||
        mapped_size >
            BOOT_FRAMEBUFFER_WINDOW_SIZE ||
        first_page > UINT64_MAX - mapped_size
    ) {
        return false;
    }

    for (
        uint64_t offset = 0;
        offset < mapped_size;
        offset += MEMORY_FRAME_SIZE
    ) {
        if (
            !boot_physical_range_check_mapping(
                first_page + offset
            )
        ) {
            return false;
        }

        ++report->framebuffer_pages_checked;
    }

    return true;
}

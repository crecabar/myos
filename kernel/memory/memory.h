// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_MEMORY_MEMORY_H
#define MYOS_MEMORY_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MEMORY_FRAME_SIZE 4096ULL

enum memory_region_type {
    MEMORY_REGION_USABLE,
    MEMORY_REGION_RESERVED,
    MEMORY_REGION_ACPI_RECLAIMABLE,
    MEMORY_REGION_ACPI_NVS,
    MEMORY_REGION_BAD_MEMORY,
    MEMORY_REGION_BOOTLOADER_RECLAIMABLE,
    MEMORY_REGION_KERNEL,
    MEMORY_REGION_FRAMEBUFFER,
};

struct memory_region {
    uint64_t base;
    uint64_t length;
    enum memory_region_type type;
};

void memory_init(
    uint64_t offset,
    const struct memory_region *memory_regions,
    size_t memory_region_count
);

/**
 * Returns the direct-map virtual address corresponding to a physical address.
 *
 * This operation only performs address translation arithmetic and does not
 * guarantee that the resulting virtual address is currently mapped.
 *
 * @param physical_address Physical address to translate.
 *
 * @return Corresponding direct-map virtual address.
 */
void *memory_physical_to_virtual(uint64_t physical_address);

/**
 * Returns the virtual base of the kernel direct physical-memory map.
 *
 * @return Direct-map virtual base address.
 */
uint64_t memory_direct_map_base(void);

/**
 * Returns the exclusive upper bound of physical memory managed by the
 * physical-frame allocator.
 *
 * @return Exclusive physical-address limit, aligned to the frame size.
 */
uint64_t memory_managed_physical_limit(void);

/**
 * Reports whether a complete physical frame lies inside a region marked
 * bootloader-reclaimable by the boot memory map.
 *
 * @param physical_address Frame-aligned physical address.
 *
 * @return true when the complete frame is bootloader-reclaimable; false
 *         otherwise.
 */
bool memory_physical_frame_is_bootloader_reclaimable(
    uint64_t physical_address
);

/**
 * Reports whether a physical frame is still reserved for the bootloader and
 * has not yet been transferred to MyOS.
 *
 * Frames below the physical allocator's minimum address are not eligible
 * under the current reclamation policy.
 *
 * @param physical_address Frame-aligned physical address.
 *
 * @return true when the frame is pending reclamation; false otherwise.
 */
bool memory_bootloader_frame_reclaim_pending(
    uint64_t physical_address
);

/**
 * Summarizes ownership of eligible bootloader-reclaimable physical frames.
 *
 * Transferred frames are classified according to their current allocator
 * state, regardless of whether they have been allocated again by MyOS.
 */
struct memory_bootloader_frame_inventory {
    uint64_t eligible_frames;
    uint64_t pending_frames;
    uint64_t transferred_free_frames;
    uint64_t transferred_used_frames;
};

/**
 * Collects a read-only inventory of eligible bootloader-reclaimable frames.
 *
 * Frames below the first MiB and partially covered boundary frames are
 * excluded under the current physical-memory policy.
 *
 * Does not inspect frame contents or change allocator ownership.
 *
 * @param inventory Receives the resulting inventory.
 *
 * @return true when the inventory was collected; false for a NULL argument.
 */
bool memory_bootloader_frame_inventory_collect(
    struct memory_bootloader_frame_inventory *inventory
);

/**
 * Transfers one bootloader-reclaimable physical frame to the MyOS allocator.
 *
 * The caller must establish that the frame is no longer referenced by any
 * live bootloader or kernel structure before calling this function.
 *
 * A successful transfer clears the reclamation-pending state permanently and
 * makes the frame available for future physical allocations. Reclaiming the
 * same frame a second time is rejected, even if MyOS has since allocated it.
 *
 * Frames below the allocator's minimum physical address are not eligible.
 *
 * @param physical_address Frame-aligned physical address.
 *
 * @return true when ownership was transferred; false when the frame is
 *         ineligible, has already been reclaimed, or has an invalid state.
 */
bool memory_bootloader_frame_reclaim(
    uint64_t physical_address
);

bool physical_alloc_frame(uint64_t *physical_address);

/**
 * Allocates a specific physical frame if it is already available to MyOS.
 *
 * The operation rejects unaligned, unmanaged, occupied, and
 * bootloader-reclamation-pending frames. It never transfers ownership
 * from the bootloader.
 *
 * @param physical_address Frame-aligned physical address to allocate.
 *
 * @return true when the requested frame was allocated; false otherwise.
 */
bool physical_alloc_frame_at(uint64_t physical_address);

bool physical_free_frame(uint64_t physical_address);

void memory_dump_map(void);

void memory_dump_physical_allocator(void);

void memory_dump_usable_frames(void);

void memory_dump_summary(void);

uint64_t physical_free_frame_count(void);

/**
 * Returns the total number of bytes reported as usable physical memory.
 *
 * @return Total usable physical-memory bytes.
 */
uint64_t memory_usable_byte_count(void);

#endif

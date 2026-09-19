// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file boot_paging.h
 * @brief Inspection of inherited boot-time paging structures.
 */

#ifndef MYOS_MEMORY_BOOT_PAGING_H
#define MYOS_MEMORY_BOOT_PAGING_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Describes the inherited boot-time page-table hierarchy.
 *
 * Frame counters describe paging-structure references. Duplicate references
 * are reported separately and therefore make reclamation unsafe.
 */
struct boot_paging_inventory {
    uint64_t pml4_frames;
    uint64_t pdpt_frames;
    uint64_t pd_frames;
    uint64_t pt_frames;

    uint64_t duplicate_table_references;
    uint64_t active_table_overlaps;
    uint64_t non_reclaimable_table_frames;
};

/**
 * Inventories every page-table frame reachable from the original boot PML4.
 *
 * Leaf-mapped physical memory is not counted. Only paging-structure frames are
 * inspected.
 *
 * The inventory also checks whether an inherited paging-structure frame is
 * still used structurally by the active kernel address space, whether the boot
 * hierarchy contains aliased table references, and whether every inherited
 * table frame belongs to bootloader-reclaimable memory.
 *
 * @param inventory Receives the resulting inventory.
 *
 * @return true when the hierarchy could be inspected successfully; false
 *         otherwise.
 */
bool boot_paging_inventory_collect(
    struct boot_paging_inventory *inventory
);

#endif

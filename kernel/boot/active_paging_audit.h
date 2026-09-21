// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file active_paging_audit.h
 * @brief Read-only audit of active kernel page-table frames.
 */

#ifndef MYOS_BOOT_ACTIVE_PAGING_AUDIT_H
#define MYOS_BOOT_ACTIVE_PAGING_AUDIT_H

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"

/**
 * Counts paging-structure references reachable from the active CR3.
 *
 * These counters describe visited tables, not mapped data pages.
 */
struct boot_active_paging_audit_report {
    uint64_t pml4_tables;
    uint64_t pdpt_tables;
    uint64_t pd_tables;
    uint64_t pt_tables;

    uint64_t large_1g_mappings;
    uint64_t large_2m_mappings;

    uint64_t free_frames;
};

/**
 * Audits the active kernel page-table hierarchy.
 *
 * Every paging-structure frame must be inside the managed physical
 * range, outside bootloader-reclaimable regions and not pending
 * reclamation. Data-page leaves are not classified as page tables.
 *
 * The audit does not change mappings or allocator ownership.
 *
 * @param boot_info Kernel-owned boot memory map.
 * @param report Receives the read-only audit results.
 *
 * @return true when all implemented checks pass; false otherwise.
 */
bool boot_active_paging_audit(
    const struct boot_info *boot_info,
    struct boot_active_paging_audit_report *report
);

#endif

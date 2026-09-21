// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file physical_range_audit.h
 * @brief Read-only audit of physical ranges before boot-memory reclamation.
 */

#ifndef MYOS_BOOT_PHYSICAL_RANGE_AUDIT_H
#define MYOS_BOOT_PHYSICAL_RANGE_AUDIT_H

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"
#include "reclaim_preflight.h"

/**
 * Results of the physical-range checks implemented by this audit.
 *
 * A successful audit does not establish that all pending frames are safe
 * to reclaim. In particular, it does not traverse every active page-table
 * level or prove the absence of other live bootloader-data references.
 */
struct boot_physical_range_audit_report {
    uint64_t reclaimable_regions;
    uint64_t eligible_frames;
    uint64_t pending_frames;

    uint64_t kernel_image_pages_checked;
    uint64_t framebuffer_pages_checked;

    uint64_t free_frames;
};

/**
 * Validates the boot memory-map ranges and selected live physical backing.
 *
 * Rejects overlapping or malformed memory-map regions. Counts complete
 * bootloader-reclaimable frames and checks that their accounting agrees
 * with the earlier preflight.
 *
 * Also checks that the active PML4 root, kernel image pages and all mapped
 * framebuffer pages do not reside in bootloader-reclaimable regions.
 *
 * Does not inspect frame contents or change allocator ownership.
 *
 * @param boot_info Kernel-owned boot information.
 * @param preflight Previously collected read-only preflight snapshot.
 * @param report Receives the physical-range audit results.
 *
 * @return true when all implemented checks pass; false otherwise.
 */
bool boot_physical_range_audit(
    const struct boot_info *boot_info,
    const struct boot_reclaim_preflight_report *preflight,
    struct boot_physical_range_audit_report *report
);

#endif

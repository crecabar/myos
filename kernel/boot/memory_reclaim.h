// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file memory_reclaim.h
 * @brief Transfer of remaining bootloader-reclaimable frames to MyOS.
 */

#ifndef MYOS_BOOT_MEMORY_RECLAIM_H
#define MYOS_BOOT_MEMORY_RECLAIM_H

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"

/**
 * Accounting result of one completed general reclamation.
 *
 * first_reclaimed_frame is meaningful only when reclaimed_frames != 0.
 */
struct boot_memory_reclaim_result {
    uint64_t reclaimed_frames;
    uint64_t first_reclaimed_frame;

    uint64_t free_before;
    uint64_t free_after;
};

/**
 * Transfers all remaining eligible bootloader-reclaimable frames.
 *
 * Repeats the boot preflight and physical/paging audits immediately
 * before the transfer. Runs once during early bootstrap, before arch_init()
 * and before creating user processes.
 *
 * A failed prerequisite returns false without transferring frames.
 * An invariant failure after transfer begins causes a kernel panic:
 * partial reclamation cannot be rolled back safely.
 *
 * @param boot_info Kernel-owned boot information.
 * @param result Receives the completed transfer accounting.
 *
 * @return true when reclamation and accounting complete successfully;
 *         false when a prerequisite is not satisfied.
 */
bool boot_memory_reclaim_remaining(
    const struct boot_info *boot_info,
    struct boot_memory_reclaim_result *result
);

#endif

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file reclaim_preflight.h
 * @brief Read-only checks before general bootloader-memory reclamation.
 */

#ifndef MYOS_BOOT_RECLAIM_PREFLIGHT_H
#define MYOS_BOOT_RECLAIM_PREFLIGHT_H

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"

/**
 * Snapshot of the allocator state at the bootloader-memory preflight.
 */
struct boot_reclaim_preflight_report {
    struct memory_bootloader_frame_inventory inventory;
    uint64_t free_frames;
};

/**
 * Checks the currently established prerequisites for general reclamation.
 *
 * Must run on the MyOS runtime stack after inherited page-table
 * reclamation and before general bootloader-memory reclamation.
 *
 * This is a read-only diagnostic gate, not permission to transfer the
 * remaining pending frames. A later slice must establish that no live
 * kernel dependency refers to any candidate frame.
 *
 * @param boot_info Kernel-owned snapshot of boot information.
 * @param report Receives the verified accounting snapshot.
 *
 * @return true when the checked prerequisites hold; false otherwise.
 */
bool boot_reclaim_preflight(
    const struct boot_info *boot_info,
    struct boot_reclaim_preflight_report *report
);

#endif

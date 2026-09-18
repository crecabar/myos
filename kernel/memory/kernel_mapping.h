// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file kernel_mapping.h
 * @brief Explicit kernel-image virtual-memory mappings.
 */

#ifndef MYOS_MEMORY_KERNEL_MAPPING_H
#define MYOS_MEMORY_KERNEL_MAPPING_H

#include "../arch/x86_64/paging.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Installs a MyOS-owned kernel-image mapping into the active kernel address
 * space.
 *
 * The existing boot-time PML4 branch is detached but retained for later
 * reclamation. The kernel address space remains rooted at the same PML4 and
 * CR3 is reloaded after the branch replacement.
 *
 * @return true when the kernel-image branch was replaced successfully; false
 *         otherwise.
 */
bool kernel_mapping_install(void);

/**
 * Returns the boot-time kernel PML4 entry replaced by
 * kernel_mapping_install().
 *
 * @param entry Receives the detached boot-time PML4 entry.
 *
 * @return true when a replaced entry is available; false otherwise.
 */
bool kernel_mapping_replaced_branch_entry(
    uint64_t *entry
);

/**
 * Builds a detached MyOS-owned mapping of the kernel image.
 *
 * The returned address space contains only the kernel-image higher-half branch
 * required for the linked kernel sections. It does not inherit the direct map
 * or any other boot-time higher-half mappings.
 *
 * The caller owns the returned address space and all page-table frames created
 * beneath it.
 *
 * @param address_space Receives the detached kernel mapping candidate.
 *
 * @return true when the mapping was built and validated successfully; false
 *         otherwise.
 */
bool kernel_mapping_build(
    struct paging_address_space *address_space
);

/**
 * Destroys a detached kernel-image mapping created by kernel_mapping_build().
 *
 * Kernel image frames themselves are never released; only the page-table
 * hierarchy owned by the detached address space is reclaimed.
 *
 * @param address_space Detached kernel mapping to destroy.
 *
 * @return true when all owned paging structures were reclaimed successfully;
 *         false otherwise.
 */
bool kernel_mapping_destroy(
    struct paging_address_space *address_space
);

#endif

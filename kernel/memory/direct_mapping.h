// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file direct_mapping.h
 * @brief MyOS-owned direct physical-memory mapping hierarchy.
 */

#ifndef MYOS_MEMORY_DIRECT_MAPPING_H
#define MYOS_MEMORY_DIRECT_MAPPING_H

#include "../arch/x86_64/paging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Installs a MyOS-owned direct physical-memory map.
 *
 * A detached copy of the active direct-map hierarchy is built first and then
 * transferred into the active kernel address space. CR3 is reloaded after the
 * cutover.
 *
 * The replaced boot-time PML4 branch is retained for later reclamation.
 *
 * @return true when the direct-map branch was replaced successfully; false
 *         otherwise.
 */
bool direct_mapping_install(void);

/**
 * Resolves a complete physical range through the MyOS-owned direct map.
 *
 * Every page touched by the range must be mapped to the expected physical
 * address in the kernel address space. The function therefore establishes
 * both arithmetic validity and actual paging coverage.
 *
 * The returned pointer borrows the direct-map mapping and remains valid only
 * while that mapping remains installed.
 *
 * @param physical_address First physical byte in the range.
 * @param size Number of bytes in the range.
 * @param virtual_address Receives the corresponding direct-map address.
 *
 * @return true when the complete range is directly mapped to the expected
 *         physical addresses; false otherwise.
 */
bool direct_mapping_resolve_physical_range(
    uint64_t physical_address,
    size_t size,
    const void **virtual_address
);

/**
 * Returns the boot-time direct-map PML4 entry replaced by
 * direct_mapping_install().
 *
 * @param entry Receives the detached boot-time PML4 entry.
 *
 * @return true when a replaced entry is available; false otherwise.
 */
bool direct_mapping_replaced_branch_entry(
    uint64_t *entry
);

/**
 * Builds a detached MyOS-owned copy of the active direct-map hierarchy.
 *
 * Physical leaves and their cache attributes are preserved. All newly
 * allocated paging-structure frames are owned by the returned address space.
 * The resulting direct-map branch is supervisor-only and non-executable.
 *
 * @param address_space Receives the detached direct-map candidate.
 *
 * @return true when the hierarchy was cloned and validated; false otherwise.
 */
bool direct_mapping_build(
    struct paging_address_space *address_space
);

/**
 * Releases a detached direct-map candidate.
 *
 * Only page-table frames owned by the candidate are released. Physical memory
 * referenced by leaf mappings is borrowed and is never freed.
 *
 * The function also accepts a candidate whose direct-map PML4 branch has
 * already been transferred away, in which case only its empty PML4 root is
 * released.
 *
 * @param address_space Detached direct-map candidate.
 *
 * @return true when all owned paging structures were released; false
 *         otherwise.
 */
bool direct_mapping_destroy(
    struct paging_address_space *address_space
);

#endif

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file device_mapping.h
 * @brief Kernel-owned virtual mappings for framebuffer and MMIO devices.
 */

#ifndef MYOS_MEMORY_DEVICE_MAPPING_H
#define MYOS_MEMORY_DEVICE_MAPPING_H

#include <stdbool.h>
#include <stdint.h>

struct framebuffer;

/**
 * Rehomes the active framebuffer into the MyOS-owned kernel device window.
 *
 * The framebuffer remains backed by the same physical memory but receives a
 * new writable, supervisor-only, non-executable, write-combining virtual
 * mapping.
 *
 * @param framebuffer Framebuffer whose virtual address will be replaced.
 *
 * @return true when the complete framebuffer was mapped successfully; false
 *         otherwise.
 */
bool device_mapping_map_framebuffer(
    struct framebuffer *framebuffer
);

/**
 * Maps one physical MMIO page into the MyOS-owned kernel device window.
 *
 * MMIO mappings are writable, supervisor-only, non-executable and
 * uncacheable.
 *
 * @param physical_address 4 KiB-aligned physical MMIO address.
 * @param virtual_address Receives the new kernel virtual address.
 *
 * @return true when the mapping was created; false otherwise.
 */
bool device_mapping_map_mmio_page(
    uint64_t physical_address,
    volatile void **virtual_address
);

#endif

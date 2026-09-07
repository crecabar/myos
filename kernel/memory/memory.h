// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_MEMORY_MEMORY_H
#define MYOS_MEMORY_MEMORY_H

#include <stddef.h>
#include <stdint.h>

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
    const struct memory_region *regions,
    size_t region_count
);

void *memory_physical_to_virtual(uint64_t physical_address);

void memory_dump_map(void);

void memory_dump_usable_frames(void);

#endif

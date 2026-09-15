// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_BOOT_BOOT_H
#define MYOS_BOOT_BOOT_H

#include <stdint.h>

#include "../drivers/framebuffer.h"
#include "../memory/memory.h"

#define BOOT_MEMORY_REGION_MAX 128

struct boot_info {
    uint64_t direct_map_offset;

    void *smbios_entry_32;
    void *smbios_entry_64;

    const char *command_line;

    struct framebuffer framebuffer;

    struct memory_region memory_regions[
        BOOT_MEMORY_REGION_MAX
    ];

    size_t memory_region_count;
};

void boot_init(
    struct boot_info *boot_info
);

#endif

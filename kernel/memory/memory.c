// SPDX-License-Identifier: GPL-2.0-only

#include "memory.h"
#include "../core/panic.h"

#include <stdbool.h>
#include <stdint.h>

static uint64_t direct_map_offset;
static bool memory_initialized;

void memory_init(uint64_t offset)
{
    direct_map_offset = offset;
    memory_initialized = true;
}

void *memory_physical_to_virtual(
    uint64_t physical_address)
{
    if (!memory_initialized) {
        kernel_panic(
            "Memory subsystem not initialized"
        );
    }

    return (void *) (
        physical_address + direct_map_offset
    );
}

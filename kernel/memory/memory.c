// SPDX-License-Identifier: GPL-2.0-only

#include "memory.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEMORY_REGION_MAX 128

static struct memory_region regions[MEMORY_REGION_MAX];
static size_t region_count;

static uint64_t direct_map_offset;
static bool memory_initialized;

void memory_init(
    uint64_t offset,
    const struct memory_region *memory_regions,
    size_t memory_region_count)
{
    if (memory_initialized) {
        kernel_panic(
            "Memory subsystem already initialized"
        );
    }

    if (memory_regions == NULL &&
        memory_region_count != 0)
    {
        kernel_panic(
            "Memory regions are NULL"
        );
    }

    if (memory_region_count > MEMORY_REGION_MAX) {
        kernel_panic(
            "Memory region count exceeds capacity"
        );
    }

    direct_map_offset =
        offset;

    for (size_t index = 0;
         index < memory_region_count;
         ++index)
    {
        regions[index] =
            memory_regions[index];
    }

    region_count =
        memory_region_count;

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

static const char *memory_region_type_name(
    enum memory_region_type type)
{
    switch (type) {
        case MEMORY_REGION_USABLE:
            return "usable";

        case MEMORY_REGION_RESERVED:
            return "reserved";

        case MEMORY_REGION_ACPI_RECLAIMABLE:
            return "acpi-reclaimable";

        case MEMORY_REGION_ACPI_NVS:
            return "acpi-nvs";

        case MEMORY_REGION_BAD_MEMORY:
            return "bad-memory";

        case MEMORY_REGION_BOOTLOADER_RECLAIMABLE:
            return "bootloader-reclaimable";

        case MEMORY_REGION_KERNEL:
            return "kernel";

        case MEMORY_REGION_FRAMEBUFFER:
            return "framebuffer";
    }

    return "unknown";
}

static uint64_t align_up(
    uint64_t value,
    uint64_t alignment)
{
    return (
        value + alignment - 1
    ) & ~(alignment - 1);
}

static uint64_t align_down(
    uint64_t value,
    uint64_t alignment)
{
    return value & ~(alignment - 1);
}

void memory_dump_map(void)
{
    if (!memory_initialized) {
        kernel_panic(
            "Memory subsystem not initialized"
        );
    }

    diagnostics_printf(
        "Memory map: %u regions\n",
        (uint64_t) region_count
    );

    for (size_t index = 0;
         index < region_count;
         ++index)
    {
        const struct memory_region *region =
            &regions[index];

        diagnostics_printf(
            "  [%u] "
            "base=%x "
            "end=%x "
            "length=%u "
            "type=%s\n",
            (uint64_t) index,
            region->base,
            region->base + region->length,
            region->length,
            memory_region_type_name(region->type)
        );
    }
}

void memory_dump_usable_frames(void)
{
    if (!memory_initialized) {
        kernel_panic(
            "Memory subsystem not initialized"
        );
    }

    uint64_t total_frames = 0;

    diagnostics_write(
        "Usable physical memory:\n"
    );

    for (size_t index = 0;
         index < region_count;
         ++index)
    {
        const struct memory_region *region =
            &regions[index];

        if (region->type != MEMORY_REGION_USABLE) {
            continue;
        }

        uint64_t region_end =
            region->base + region->length;

        uint64_t usable_start =
            align_up(
                region->base,
                MEMORY_FRAME_SIZE
            );

        uint64_t usable_end =
            align_down(
                region_end,
                MEMORY_FRAME_SIZE
            );

        if (usable_end <= usable_start) {
            continue;
        }

        uint64_t usable_length =
            usable_end - usable_start;

        uint64_t frame_count =
            usable_length / MEMORY_FRAME_SIZE;

        total_frames +=
            frame_count;

        diagnostics_printf(
            "  base=%x "
            "end=%x "
            "frames=%u\n",
            usable_start,
            usable_end,
            frame_count
        );
    }

    diagnostics_printf(
        "Total usable frames=%u\n"
        "Total usable bytes=%u\n",
        total_frames,
        total_frames * MEMORY_FRAME_SIZE
    );
}

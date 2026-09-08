// SPDX-License-Identifier: GPL-2.0-only

#include "memory.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEMORY_REGION_MAX 128
#define MEMORY_FRAME_SIZE 4096ULL
#define MEMORY_BITMAP_MIN_ADDRESS 0x100000ULL

static struct memory_region regions[MEMORY_REGION_MAX];
static size_t region_count;

static uint64_t direct_map_offset;
static bool memory_initialized;

static uint8_t *frame_bitmap;
static uint64_t frame_bitmap_physical;
static size_t frame_bitmap_size;
static uint64_t managed_frame_count;

/* Helpers and private functions */
static uint64_t memory_highest_usable_address(void);
static const char *memory_region_type_name(enum memory_region_type type);
static uint64_t align_up(uint64_t value, uint64_t alignment);
static uint64_t align_down(uint64_t value, uint64_t alignment);
static size_t frame_bitmap_size_for(uint64_t frame_count);
static uint64_t bytes_to_frames(size_t byte_count);
static uint64_t find_bitmap_physical_address(uint64_t bitmap_frame_count);
static void *direct_map_physical_address(uint64_t physical_address);
static void frame_bitmap_mark_all_used(void);
static void frame_bitmap_set(uint64_t frame_number, bool used);
static bool frame_bitmap_is_used(uint64_t frame_number);
static uint64_t frame_bitmap_count_free(void);
static void frame_bitmap_mark_region_free(const struct memory_region *region);
static void frame_bitmap_release_usable_regions(void);
static void frame_bitmap_mark_range_used(uint64_t physical_start, uint64_t physical_end);
/* END HELPERS */

void memory_init(
    uint64_t offset,
    const struct memory_region *memory_regions,
    size_t memory_region_count)
{
    if (memory_initialized) {
        kernel_panic("Memory subsystem already initialized");
    }

    if (memory_regions == NULL && memory_region_count != 0)
    {
        kernel_panic("Memory regions are NULL");
    }

    if (memory_region_count > MEMORY_REGION_MAX) {
        kernel_panic("Memory region count exceeds capacity");
    }

    direct_map_offset = offset;

    for (size_t index = 0; index < memory_region_count; ++index)
    {
        regions[index] = memory_regions[index];
    }

    region_count = memory_region_count;
    uint64_t highest_usable_address = memory_highest_usable_address();
    managed_frame_count = highest_usable_address / MEMORY_FRAME_SIZE;
    frame_bitmap_size = frame_bitmap_size_for(managed_frame_count);
    uint64_t bitmap_frame_count = bytes_to_frames(frame_bitmap_size);
    frame_bitmap_physical = find_bitmap_physical_address(bitmap_frame_count);
    frame_bitmap = direct_map_physical_address(frame_bitmap_physical);

    frame_bitmap_mark_all_used();
    frame_bitmap_release_usable_regions();
    frame_bitmap_mark_range_used(0, MEMORY_BITMAP_MIN_ADDRESS);

    uint64_t bitmap_end = frame_bitmap_physical + bitmap_frame_count * MEMORY_FRAME_SIZE;
    frame_bitmap_mark_range_used(frame_bitmap_physical, bitmap_end);
    uint64_t free_frame_count = frame_bitmap_count_free();

    diagnostics_printf(
        "Physical frame bitmap sizing:\n"
        "  managed frames=%u\n"
        "  bitmap bytes=%u\n"
        "  bitmap frames=%u\n"
        "  bitmap PA=%x\n"
        "  bitmap VA=%x\n"
        "  free frames=%u\n",
        managed_frame_count,
        (uint64_t) frame_bitmap_size,
        bitmap_frame_count,
        frame_bitmap_physical,
        (uint64_t) frame_bitmap,
        free_frame_count
    );

    memory_initialized = true;
}

void *memory_physical_to_virtual(uint64_t physical_address)
{
    if (!memory_initialized) {
        kernel_panic("Memory subsystem not initialized");
    }

    return direct_map_physical_address(physical_address);
}

static const char *memory_region_type_name(enum memory_region_type type)
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

static uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (
        value + alignment - 1
    ) & ~(alignment - 1);
}

static uint64_t align_down(uint64_t value, uint64_t alignment)
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

static uint64_t memory_highest_usable_address(void)
{
    uint64_t highest_address = 0;

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

        if (region_end > highest_address) {
            highest_address =
                region_end;
        }
    }

    return highest_address;
}

static size_t frame_bitmap_size_for(uint64_t frame_count)
{
    return (size_t) (
        (frame_count + 7) / 8
    );
}

static uint64_t bytes_to_frames(size_t byte_count)
{
    return (
        byte_count +
        MEMORY_FRAME_SIZE -
        1
    ) / MEMORY_FRAME_SIZE;
}

static uint64_t find_bitmap_physical_address(uint64_t bitmap_frame_count)
{
    uint64_t required_bytes =
        bitmap_frame_count *
        MEMORY_FRAME_SIZE;

    for (size_t index = 0; index < region_count; ++index)
    {
        const struct memory_region *region = &regions[index];

        if (region->type != MEMORY_REGION_USABLE) {
            continue;
        }

        uint64_t start = region->base;

        uint64_t end = region->base + region->length;

        if (start < MEMORY_BITMAP_MIN_ADDRESS) {
            start =
                MEMORY_BITMAP_MIN_ADDRESS;
        }

        start = align_up(start,MEMORY_FRAME_SIZE);

        if (end <= start) {
            continue;
        }

        if ((end - start) >= required_bytes) {
            return start;
        }
    }

    kernel_panic(
        "Unable to reserve memory for frame bitmap"
    );
}

static void *direct_map_physical_address(uint64_t physical_address)
{
    return (void *) (
        physical_address +
        direct_map_offset
    );
}

static void frame_bitmap_mark_all_used(void)
{
    for (size_t index = 0; index < frame_bitmap_size; ++index) {
        frame_bitmap[index] = 0xff;
    }
}

static void frame_bitmap_set(uint64_t frame_number, bool used)
{
    if (frame_number >= managed_frame_count) {
        kernel_panic("Frame number exceeds bitmap capacity");
    }

    size_t byte_index = (size_t) (frame_number / 8);
    uint8_t bit_index = (uint8_t) (frame_number % 8);
    uint8_t mask = (uint8_t) (1U << bit_index);

    if (used) {
        frame_bitmap[byte_index] |= mask;
    } else {
        frame_bitmap[byte_index] &= (uint8_t) ~mask;
    }
}

static bool frame_bitmap_is_used(uint64_t frame_number)
{
    if (frame_number >= managed_frame_count) {
        kernel_panic("Frame number exceeds bitmap capacity");
    }

    size_t byte_index = (size_t) (frame_number / 8);
    uint8_t bit_index = (uint8_t) (frame_number % 8);
    uint8_t mask = (uint8_t) (1U << bit_index);

    return (frame_bitmap[byte_index] & mask) != 0;
}

static uint64_t frame_bitmap_count_free(void)
{
    uint64_t free_frames = 0;

    for (uint64_t frame = 0; frame < managed_frame_count; ++frame) {
        if (!frame_bitmap_is_used(frame)) {
            ++free_frames;
        }
    }

    return free_frames;
}

static void frame_bitmap_mark_region_free(const struct memory_region *region)
{
    uint64_t start = align_up(region->base, MEMORY_FRAME_SIZE);
    uint64_t end = align_down(
        region->base + region->length,
        MEMORY_FRAME_SIZE
    );

    if (end <= start) {
        return;
    }

    uint64_t first_frame = start / MEMORY_FRAME_SIZE;
    uint64_t last_frame = end / MEMORY_FRAME_SIZE;

    for (uint64_t frame = first_frame; frame < last_frame; ++frame) {
        frame_bitmap_set(frame, false);
    }
}

static void frame_bitmap_release_usable_regions(void)
{
    for (size_t index = 0; index < region_count; ++index) {
        if (regions[index].type != MEMORY_REGION_USABLE) {
            continue;
        }

        frame_bitmap_mark_region_free(&regions[index]);
    }
}

static void frame_bitmap_mark_range_used(uint64_t physical_start, uint64_t physical_end)
{
    uint64_t start = align_down(physical_start, MEMORY_FRAME_SIZE);
    uint64_t end = align_up(physical_end, MEMORY_FRAME_SIZE);

    uint64_t first_frame = start / MEMORY_FRAME_SIZE;
    uint64_t last_frame = end / MEMORY_FRAME_SIZE;

    if (last_frame > managed_frame_count) {
        last_frame = managed_frame_count;
    }

    for (uint64_t frame = first_frame; frame < last_frame; ++frame) {
        frame_bitmap_set(frame, true);
    }
}

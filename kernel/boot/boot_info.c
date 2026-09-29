// SPDX-License-Identifier: GPL-2.0-only

#include "boot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static bool boot_info_command_line_valid(
    const struct boot_info *boot_info
);

static bool boot_info_framebuffer_valid(
    const struct framebuffer *framebuffer
);

static bool boot_info_memory_regions_valid(
    const struct boot_info *boot_info
);

static bool boot_info_memory_region_type_valid(
    enum memory_region_type type
);

static bool boot_info_modules_valid(
    const struct boot_info *boot_info
);

static bool boot_info_text_terminated(
    const char *text,
    size_t capacity
);

// Public functions implementations
bool boot_info_validate(
    const struct boot_info *boot_info)
{
    if (boot_info == NULL) {
        return false;
    }

    if (
        boot_info->rsdp_snapshot_size != 0 &&
        boot_info->rsdp_snapshot_size !=
            BOOT_ACPI_RSDP_V1_SIZE &&
        boot_info->rsdp_snapshot_size !=
            BOOT_ACPI_RSDP_V2_SIZE
    ) {
        return false;
    }

    if (!boot_info_command_line_valid(
        boot_info
    )) {
        return false;
    }

    if (!boot_info_framebuffer_valid(
        &boot_info->framebuffer
    )) {
        return false;
    }

    if (!boot_info_memory_regions_valid(
        boot_info
    )) {
        return false;
    }

    if (!boot_info_modules_valid(
        boot_info
    )) {
        return false;
    }

    return true;
}

// Private functions and helpers implementations
static bool boot_info_command_line_valid(
    const struct boot_info *boot_info)
{
    if (
        boot_info->command_line_length >=
        BOOT_COMMAND_LINE_MAX
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < boot_info->command_line_length;
        ++index
    ) {
        if (
            boot_info->command_line[index] ==
            '\0'
        ) {
            return false;
        }
    }

    return
        boot_info->command_line[
            boot_info->command_line_length
        ] == '\0';
}

static bool boot_info_framebuffer_valid(
    const struct framebuffer *framebuffer)
{
    if (framebuffer == NULL) return false;
    if (framebuffer->address == NULL) return false;
    if (framebuffer->width == 0) return false;
    if (framebuffer->height == 0) return false;
    if (framebuffer->pitch == 0) return false;
    if (framebuffer->bpp != 32) return false;

    if (
        framebuffer->red_mask_size != 8 ||
        framebuffer->green_mask_size != 8 ||
        framebuffer->blue_mask_size != 8
    ) {
        return false;
    }

    if (
        framebuffer->red_mask_shift > 24 ||
        framebuffer->green_mask_shift > 24 ||
        framebuffer->blue_mask_shift > 24
    ) {
        return false;
    }

    uint32_t red_mask =
        0xffU <<
        framebuffer->red_mask_shift;

    uint32_t green_mask =
        0xffU <<
        framebuffer->green_mask_shift;

    uint32_t blue_mask =
        0xffU <<
        framebuffer->blue_mask_shift;

    if (
        (red_mask & green_mask) != 0 ||
        (red_mask & blue_mask) != 0 ||
        (green_mask & blue_mask) != 0
    ) {
        return false;
    }

    if (
        framebuffer->width >
        UINT64_MAX / sizeof(uint32_t)
    ) {
        return false;
    }

    uint64_t visible_row_bytes =
        framebuffer->width *
        sizeof(uint32_t);

    if (
        framebuffer->pitch <
        visible_row_bytes
    ) {
        return false;
    }

    if (
        framebuffer->pitch >
        UINT64_MAX / framebuffer->height
    ) {
        return false;
    }

    return true;
}

static bool boot_info_memory_regions_valid(
    const struct boot_info *boot_info)
{
    if (
        boot_info->memory_region_count == 0 ||
        boot_info->memory_region_count >
            BOOT_MEMORY_REGION_MAX
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < boot_info->memory_region_count;
        ++index
    ) {
        const struct memory_region *region =
            &boot_info->memory_regions[index];

        if (
            region->length == 0 ||
            region->length >
                UINT64_MAX - region->base ||
            !boot_info_memory_region_type_valid(
                region->type
            )
        ) {
            return false;
        }

        uint64_t region_end =
            region->base +
            region->length;

        for (
            size_t other_index = index + 1;
            other_index <
                boot_info->memory_region_count;
            ++other_index
        ) {
            const struct memory_region *other =
                &boot_info->memory_regions[
                    other_index
                ];

            if (
                other->length == 0 ||
                other->length >
                    UINT64_MAX - other->base
            ) {
                return false;
            }

            uint64_t other_end =
                other->base +
                other->length;

            if (
                region->base < other_end &&
                other->base < region_end
            ) {
                return false;
            }
        }
    }

    return true;
}

static bool boot_info_memory_region_type_valid(
    enum memory_region_type type)
{
    switch (type) {
        case MEMORY_REGION_USABLE:
        case MEMORY_REGION_RESERVED:
        case MEMORY_REGION_ACPI_RECLAIMABLE:
        case MEMORY_REGION_ACPI_NVS:
        case MEMORY_REGION_BAD_MEMORY:
        case MEMORY_REGION_BOOTLOADER_RECLAIMABLE:
        case MEMORY_REGION_KERNEL:
        case MEMORY_REGION_FRAMEBUFFER:
            return true;
    }

    return false;
}

static bool boot_info_modules_valid(
    const struct boot_info *boot_info)
{
    if (
        boot_info->module_count >
        BOOT_MODULE_MAX
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < boot_info->module_count;
        ++index
    ) {
        const struct boot_module *module =
            &boot_info->modules[index];

        if (module->size == 0) {
            return false;
        }

        if (
            module->size >
                UINT64_MAX -
                module->physical_base ||
            module->size >
                UINT64_MAX -
                module->virtual_base
        ) {
            return false;
        }

        if (
            !boot_info_text_terminated(
                module->name,
                BOOT_MODULE_NAME_MAX
            ) ||
            !boot_info_text_terminated(
                module->command_line,
                BOOT_MODULE_COMMAND_LINE_MAX
            )
        ) {
            return false;
        }
    }

    return true;
}

static bool boot_info_text_terminated(
    const char *text,
    size_t capacity)
{
    if (text == NULL || capacity == 0) {
        return false;
    }

    for (
        size_t index = 0;
        index < capacity;
        ++index
    ) {
        if (text[index] == '\0') {
            return true;
        }
    }

    return false;
}

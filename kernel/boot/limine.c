// SPDX-License-Identifier: GPL-2.0-only

#include "boot.h"
#include "../core/panic.h"

#include <stddef.h>
#include <stdint.h>

#include <limine.h>

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

static enum memory_region_type memory_region_type_from_limine(uint64_t limine_type)
{
    switch (limine_type) {
        case LIMINE_MEMMAP_USABLE:
            return MEMORY_REGION_USABLE;

        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
            return MEMORY_REGION_ACPI_RECLAIMABLE;

        case LIMINE_MEMMAP_ACPI_NVS:
            return MEMORY_REGION_ACPI_NVS;

        case LIMINE_MEMMAP_BAD_MEMORY:
            return MEMORY_REGION_BAD_MEMORY;

        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            return MEMORY_REGION_BOOTLOADER_RECLAIMABLE;

        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
            return MEMORY_REGION_KERNEL;

        case LIMINE_MEMMAP_FRAMEBUFFER:
            return MEMORY_REGION_FRAMEBUFFER;

        default:
            return MEMORY_REGION_RESERVED;
    }
}

void boot_init(struct boot_info *boot_info)
{
    if (boot_info == NULL) {
        kernel_panic(
            "boot_init received NULL boot_info"
        );
    }

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        kernel_panic(
            "Unsupported Limine base revision"
        );
    }

    if (framebuffer_request.response == NULL) {
        kernel_panic(
            "Framebuffer unavailable"
        );
    }

    if (hhdm_request.response == NULL) {
        kernel_panic(
            "HHDM unavailable"
        );
    }

    if (memmap_request.response == NULL) {
        kernel_panic(
            "Memory map unavailable"
        );
    }

    struct limine_framebuffer_response *framebuffer_response =
        framebuffer_request.response;

    if (framebuffer_response->framebuffer_count == 0) {
        kernel_panic(
            "No framebuffer entries"
        );
    }

    if (framebuffer_response->framebuffers[0] == NULL) {
        kernel_panic(
            "Framebuffer entry missing"
        );
    }

    struct limine_framebuffer *limine_framebuffer =
        framebuffer_response->framebuffers[0];

    if (limine_framebuffer->bpp != 32) {
        kernel_panic(
            "Unsupported framebuffer bpp"
        );
    }

    if (limine_framebuffer->red_mask_size != 8 ||
        limine_framebuffer->green_mask_size != 8 ||
        limine_framebuffer->blue_mask_size != 8)
    {
        kernel_panic(
            "Unsupported framebuffer color layout"
        );
    }

    boot_info->framebuffer.address =
        limine_framebuffer->address;

    boot_info->framebuffer.width =
        limine_framebuffer->width;

    boot_info->framebuffer.height =
        limine_framebuffer->height;

    boot_info->framebuffer.pitch =
        limine_framebuffer->pitch;

    boot_info->framebuffer.bpp =
        limine_framebuffer->bpp;

    boot_info->framebuffer.red_mask_size =
        limine_framebuffer->red_mask_size;

    boot_info->framebuffer.red_mask_shift =
        limine_framebuffer->red_mask_shift;

    boot_info->framebuffer.green_mask_size =
        limine_framebuffer->green_mask_size;

    boot_info->framebuffer.green_mask_shift =
        limine_framebuffer->green_mask_shift;

    boot_info->framebuffer.blue_mask_size =
        limine_framebuffer->blue_mask_size;

    boot_info->framebuffer.blue_mask_shift =
        limine_framebuffer->blue_mask_shift;

    boot_info->direct_map_offset =
        hhdm_request.response->offset;

    struct limine_memmap_response *memmap_response =
        memmap_request.response;

    if (memmap_response->entry_count >
        BOOT_MEMORY_REGION_MAX)
    {
        kernel_panic(
            "Too many memory map entries"
        );
    }

    boot_info->memory_region_count =
        (size_t) memmap_response->entry_count;

    for (size_t index = 0;
         index < boot_info->memory_region_count;
         ++index)
    {
        struct limine_memmap_entry *entry =
            memmap_response->entries[index];

        if (entry == NULL) {
            kernel_panic(
                "Invalid memory map entry"
            );
        }

        boot_info->memory_regions[index].base =
            entry->base;

        boot_info->memory_regions[index].length =
            entry->length;

        boot_info->memory_regions[index].type =
            memory_region_type_from_limine(
                entry->type
            );
    }
}


// SPDX-License-Identifier: GPL-2.0-only

#include "boot.h"
#include "../core/panic.h"

#include <stdbool.h>
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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_smbios_request smbios_request = {
    .id = LIMINE_SMBIOS_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_cmdline_request
    executable_cmdline_request = {
        .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
        .revision = 0,
        .response = NULL,
    };

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

/*
 * Stored in kernel-owned .bss, not in bootloader-reclaimable memory.
 */
static bool boot_snapshot_complete;

static void boot_command_line_from_limine(
    struct boot_info *boot_info,
    const char *command_line
);

static void boot_modules_from_limine(
    struct boot_info *boot_info
);

static void boot_module_text_from_limine(
    char *destination,
    size_t capacity,
    const char *source,
    const char *failure_message
);

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

    if (boot_snapshot_complete) {
        kernel_panic(
            "Boot protocol snapshot already completed"
        );
    }

    boot_info->smbios_entry_32 = NULL;
    boot_info->smbios_entry_64 = NULL;

    boot_info->rsdp_snapshot_size = 0;

    for (size_t index = 0;
         index < BOOT_ACPI_RSDP_V2_SIZE;
         ++index) {
        boot_info->rsdp_snapshot[index] = 0;
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

    boot_info->direct_map_offset = hhdm_request.response->offset;

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

    /*
     * Copy the RSDP header while Limine's response and the
     * firmware-provided address are still accessible.
     *
     * Never retain the response pointer or its address in
     * persistent boot information.
     */
    if (
        rsdp_request.response != NULL &&
        rsdp_request.response->address != NULL
    ) {
        const uint8_t *rsdp =
            (const uint8_t *) rsdp_request.response->address;

        for (size_t index = 0;
            index < BOOT_ACPI_RSDP_V1_SIZE;
            ++index) {
            boot_info->rsdp_snapshot[index] = rsdp[index];
        }

        boot_info->rsdp_snapshot_size =
           BOOT_ACPI_RSDP_V1_SIZE;

        /*
         * Revision is byte 15 of the RSDP. ACPI 2.0 and
         * later define an extended header.
         *
         * Its signature, checksums and declared length
         * have not yet been validated at this stage.
         */
        if (boot_info->rsdp_snapshot[15] >= 2) {
            for (size_t index = BOOT_ACPI_RSDP_V1_SIZE;
                    index < BOOT_ACPI_RSDP_V2_SIZE;
                    ++index) {
                boot_info->rsdp_snapshot[index] = rsdp[index];
            }

            boot_info->rsdp_snapshot_size =
                BOOT_ACPI_RSDP_V2_SIZE;
        }
    }

    if (smbios_request.response != NULL) {
        boot_info->smbios_entry_32 =
            smbios_request.response->entry_32;

        boot_info->smbios_entry_64 =
            smbios_request.response->entry_64;
    }

    boot_info->command_line[0] = '\0';
    boot_info->command_line_length = 0;

    if (
        executable_cmdline_request.response != NULL
    ) {
        boot_command_line_from_limine(
            boot_info,
            executable_cmdline_request.response->cmdline
        );
    }

    boot_modules_from_limine(
        boot_info
    );

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

    /*
     * Every Limine response used by boot_init() has now been consumed.
     *
     * Clear these persistent references while .limine_requests is still
     * writable. kernel_mapping_install() will later remap the section
     * read-only, so this must not be deferred until kernel_main_continue().
     *
     * The boot_info SMBIOS pointers and the original framebuffer address
     * have their own, later consumption and remapping points.
     */
    framebuffer_request.response = NULL;
    hhdm_request.response = NULL;
    memmap_request.response = NULL;
    module_request.response = NULL;
    rsdp_request.response = NULL;
    smbios_request.response = NULL;
    executable_cmdline_request.response = NULL;

    boot_snapshot_complete = true;
}

bool boot_protocol_snapshot_complete(void)
{
    if (!boot_snapshot_complete) {
        return false;
    }

    /*
     * Inspect the request fields themselves; never dereference an old
     * response address after bootloader-memory reclamation.
     */
    return
        module_request.response == NULL &&
        framebuffer_request.response == NULL &&
        hhdm_request.response == NULL &&
        memmap_request.response == NULL &&
        rsdp_request.response == NULL &&
        smbios_request.response == NULL &&
        executable_cmdline_request.response == NULL;
}

static void boot_command_line_from_limine(
    struct boot_info *boot_info,
    const char *command_line)
{
    if (boot_info == NULL) {
        kernel_panic(
            "Limine command-line snapshot received NULL boot_info"
        );
    }

    if (command_line == NULL) {
        return;
    }

    size_t length = 0;

    while (command_line[length] != '\0') {
        if (
            length >=
            BOOT_COMMAND_LINE_MAX - 1
        ) {
            kernel_panic(
                "Boot command line exceeds normalized limit"
            );
        }

        boot_info->command_line[length] =
            command_line[length];

        ++length;
    }

    boot_info->command_line[length] = '\0';

    boot_info->command_line_length =
        length;
}

static void boot_modules_from_limine(
    struct boot_info *boot_info)
{
    boot_info->module_count = 0;

    if (module_request.response == NULL) {
        return;
    }

    struct limine_module_response *response =
        module_request.response;

    if (
        response->module_count >
        BOOT_MODULE_MAX
    ) {
        kernel_panic(
            "Too many boot modules"
        );
    }

    if (
        response->module_count != 0 &&
        response->modules == NULL
    ) {
        kernel_panic(
            "Boot module array missing"
        );
    }

    for (
        size_t index = 0;
        index < (size_t) response->module_count;
        ++index
    ) {
        struct limine_file *file =
            response->modules[index];

        if (file == NULL) {
            kernel_panic(
                "Boot module descriptor missing"
            );
        }

        if (
            file->address == NULL ||
            file->size == 0
        ) {
            kernel_panic(
                "Invalid boot module range"
            );
        }

        uint64_t virtual_base =
            (uint64_t) file->address;

        if (
            virtual_base <
            boot_info->direct_map_offset
        ) {
            kernel_panic(
                "Boot module address is outside HHDM"
            );
        }

        uint64_t physical_base =
            virtual_base -
            boot_info->direct_map_offset;

        if (
            file->size >
                UINT64_MAX - physical_base ||
            file->size >
                UINT64_MAX - virtual_base
        ) {
            kernel_panic(
                "Boot module range overflows"
            );
        }

        struct boot_module *module =
            &boot_info->modules[index];

        module->physical_base =
            physical_base;

        module->virtual_base =
            virtual_base;

        module->size =
            file->size;

        boot_module_text_from_limine(
            module->name,
            BOOT_MODULE_NAME_MAX,
            file->path,
            "Boot module path exceeds normalized limit"
        );

        boot_module_text_from_limine(
            module->command_line,
            BOOT_MODULE_COMMAND_LINE_MAX,
            file->string,
            "Boot module command line exceeds normalized limit"
        );
    }

    boot_info->module_count =
        (size_t) response->module_count;
}

static void boot_module_text_from_limine(
    char *destination,
    size_t capacity,
    const char *source,
    const char *failure_message)
{
    if (
        destination == NULL ||
        capacity == 0 ||
        source == NULL
    ) {
        kernel_panic(
            "Invalid boot module text"
        );
    }

    size_t length = 0;

    while (source[length] != '\0') {
        if (
            length >=
            capacity - 1
        ) {
            kernel_panic(
                failure_message
            );
        }

        destination[length] =
            source[length];

        ++length;
    }

    destination[length] = '\0';
}

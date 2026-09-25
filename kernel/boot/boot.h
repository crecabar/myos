// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_BOOT_BOOT_H
#define MYOS_BOOT_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#include "../drivers/framebuffer.h"
#include "../memory/memory.h"

#define BOOT_MEMORY_REGION_MAX 128

#define BOOT_ACPI_RSDP_V1_SIZE 20
#define BOOT_ACPI_RSDP_V2_SIZE 36

struct boot_info {
    uint64_t direct_map_offset;

    /*
     * Kernel-owned snapshot of the RSDP's standardized
     * initial bytes. A size of zero means Limine did not
     * provide an RSDP.
     *
     * The ACPI parser will validate the signature, revision,
     * checksums and declared length before consuming the data.
     */
    uint8_t rsdp_snapshot[BOOT_ACPI_RSDP_V2_SIZE];
    size_t rsdp_snapshot_size;

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

/**
 * Reports whether the boot protocol's response structures have been
 * consumed and their persistent response pointers cleared.
 *
 * This does not imply that transient data referenced by boot_info,
 * such as SMBIOS entry points, has already been consumed.
 *
 * @return true when the protocol snapshot is complete and no request
 *         retains a response pointer; false otherwise.
 */
bool boot_protocol_snapshot_complete(void);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_BOOT_BOOT_H
#define MYOS_BOOT_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../drivers/framebuffer.h"
#include "../memory/memory.h"

#define BOOT_MEMORY_REGION_MAX 128

#define BOOT_COMMAND_LINE_MAX 256

#define BOOT_MODULE_MAX 16
#define BOOT_MODULE_NAME_MAX 64
#define BOOT_MODULE_COMMAND_LINE_MAX 256

#define BOOT_ACPI_RSDP_V1_SIZE 20
#define BOOT_ACPI_RSDP_V2_SIZE 36

/**
 * Boot-provided module descriptor normalized by the active boot backend.
 *
 * The descriptor and its textual metadata live in kernel-owned boot_info
 * storage. The module byte range itself remains a boot resource until its
 * consumer establishes longer-lived ownership.
 *
 * physical_base and virtual_base identify the same first module byte in the
 * physical and early kernel virtual address spaces respectively. size is the
 * byte length of the half-open ranges beginning at those addresses.
 *
 * Backends must reject descriptors that cannot be represented without
 * truncating name or command-line metadata.
 */
struct boot_module {
    uint64_t physical_base;
    uint64_t virtual_base;
    uint64_t size;

    char name[BOOT_MODULE_NAME_MAX];

    char command_line[
        BOOT_MODULE_COMMAND_LINE_MAX
    ];
};

/**
 * Bootloader-independent kernel boot information.
 *
 * The struct itself and all inline arrays are kernel-owned. A boot backend
 * must normalize protocol-specific responses into this representation before
 * ordinary kernel initialization consumes them.
 *
 * Scalar values, memory-region descriptors, the ACPI RSDP snapshot, command
 * line, and boot-module descriptors remain valid independently of the backend
 * response structures.
 *
 * SMBIOS entry-point pointers and the initial framebuffer address are borrowed
 * boot-time references. Their consumers must finish using or remap them before
 * the corresponding boot resources are reclaimed.
 */
struct boot_info {
    /*
     * Early direct-map offset established by the boot path.
     */
    uint64_t direct_map_offset;

    /*
     * Kernel-owned snapshot of the standardized initial RSDP bytes.
     *
     * A size of zero means the active boot backend did not provide an RSDP.
     * Signature, revision, checksums and declared length are validated by the
     * ACPI layer before the snapshot is consumed.
     */
    uint8_t rsdp_snapshot[
        BOOT_ACPI_RSDP_V2_SIZE
    ];

    size_t rsdp_snapshot_size;

    /*
     * Borrowed firmware entry-point references. These are consumed during
     * early initialization and must not survive boot-resource reclamation.
     */
    void *smbios_entry_32;
    void *smbios_entry_64;

    /*
     * Kernel-owned, NUL-terminated command-line snapshot.
     */
    char command_line[
        BOOT_COMMAND_LINE_MAX
    ];

    size_t command_line_length;

    /*
     * The address initially refers to the framebuffer mapping supplied by the
     * boot path. device_mapping_map_framebuffer() later replaces it with a
     * MyOS-owned kernel virtual mapping.
     */
    struct framebuffer framebuffer;

    /*
     * Kernel-owned normalized physical-memory map.
     */
    struct memory_region memory_regions[
        BOOT_MEMORY_REGION_MAX
    ];

    size_t memory_region_count;

    /*
     * Kernel-owned module descriptors. Module contents retain separate boot
     * resource ownership until explicitly consumed or preserved.
     *
     * #164 will populate these descriptors from the supported boot backends.
     */
    struct boot_module modules[
        BOOT_MODULE_MAX
    ];

    size_t module_count;
};

/**
 * Validates the bootloader-independent boot information contract.
 *
 * This operation is read-only and does not depend on protocol-specific
 * response structures.
 *
 * @param boot_info Normalized boot information.
 *
 * @return true when the normalized contract is internally valid; false
 *         otherwise.
 */
bool boot_info_validate(
    const struct boot_info *boot_info
);

void boot_init(
    struct boot_info *boot_info
);

/**
 * Reports whether the active boot backend's response structures have been
 * consumed and its persistent protocol references cleared.
 *
 * This does not imply that transient resources referenced by boot_info, such
 * as SMBIOS entry points or boot-module contents, have already been consumed.
 *
 * @return true when the protocol snapshot is complete and the backend retains
 *         no response-structure references; false otherwise.
 */
bool boot_protocol_snapshot_complete(void);

#endif

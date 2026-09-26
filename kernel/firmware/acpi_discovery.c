// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file acpi_discovery.c
 * @brief Discovery of ACPI tables published by platform firmware.
 */

#include "acpi_discovery.h"

#include "../memory/direct_mapping.h"
#include "../memory/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool acpi_discovery_map_table(
    uint64_t physical_address,
    const uint8_t **bytes,
    size_t *size
);

static bool acpi_discovery_signature_equal(
    const uint8_t *header,
    const char *signature
);

static bool acpi_discovery_map_table(
    uint64_t physical_address,
    const uint8_t **bytes,
    size_t *size)
{
    if (bytes == NULL || size == NULL) {
        return false;
    }

    *bytes = NULL;
    *size = 0;

    if (physical_address == 0) {
        return false;
    }

    /*
     * Validate the ownership of the common header before
     * attempting to resolve or read its virtual address.
     */
    if (
        !memory_physical_range_is_acpi(
            physical_address,
            ACPI_SDT_HEADER_SIZE
        )
    ) {
        return false;
    }

    const void *header_mapping;

    if (
        !direct_mapping_resolve_physical_range(
            physical_address,
            ACPI_SDT_HEADER_SIZE,
            &header_mapping
        )
    ) {
        return false;
    }

    uint32_t declared_length;

    if (
        !acpi_sdt_length_read(
            (const uint8_t *) header_mapping,
            ACPI_SDT_HEADER_SIZE,
            &declared_length
        )
    ) {
        return false;
    }

    /*
     * The declared length is firmware-controlled. Validate
     * ownership and mapping coverage of the entire table
     * before exposing its bytes to a parser.
     */
    if (
        !memory_physical_range_is_acpi(
            physical_address,
            (size_t) declared_length
        )
    ) {
        return false;
    }

    const void *table_mapping;

    if (
        !direct_mapping_resolve_physical_range(
            physical_address,
            (size_t) declared_length,
            &table_mapping
        )
    ) {
        return false;
    }

    *bytes = (const uint8_t *) table_mapping;
    *size = (size_t) declared_length;

    return true;
}

static bool acpi_discovery_signature_equal(
    const uint8_t *header,
    const char *signature)
{
    if (header == NULL || signature == NULL) {
        return false;
    }

    for (
        size_t index = 0;
        index < ACPI_SDT_SIGNATURE_SIZE;
        ++index
    ) {
        if (
            header[index] !=
            (uint8_t) signature[index]
        ) {
            return false;
        }
    }

    return true;
}

bool acpi_madt_discover(
    const uint8_t *rsdp,
    size_t rsdp_size,
    struct acpi_madt *madt)
{
    if (madt == NULL) {
        return false;
    }

    /*
     * Never expose a partially discovered MADT.
     */
    *madt = (struct acpi_madt) {0};

    struct acpi_rsdp_info rsdp_info;

    if (
        !acpi_rsdp_parse(
            rsdp,
            rsdp_size,
            &rsdp_info
        )
    ) {
        return false;
    }

    uint64_t root_physical_address;
    enum acpi_root_table_kind expected_kind;

    /*
     * Prefer XSDT when the RSDP supplies one; otherwise
     * use the legacy RSDT.
     */
    if (
        rsdp_info.revision >= 2 &&
        rsdp_info.xsdt_physical_address != 0
    ) {
        root_physical_address =
            rsdp_info.xsdt_physical_address;

        expected_kind =
            ACPI_ROOT_TABLE_XSDT;
    } else {
        root_physical_address =
            (uint64_t)
            rsdp_info.rsdt_physical_address;

        expected_kind =
            ACPI_ROOT_TABLE_RSDT;
    }

    const uint8_t *root_bytes;
    size_t root_size;

    if (
        !acpi_discovery_map_table(
            root_physical_address,
            &root_bytes,
            &root_size
        )
    ) {
        return false;
    }

    struct acpi_root_table root;

    if (
        !acpi_root_table_parse(
            root_bytes,
            root_size,
            &root
        )
    ) {
        return false;
    }

    if (
        root.kind != expected_kind ||
        root.entry_count == 0
    ) {
        return false;
    }

    bool found = false;
    struct acpi_madt candidate = {0};

    for (
        size_t index = 0;
        index < root.entry_count;
        ++index
    ) {
        uint64_t table_physical_address;

        if (
            !acpi_root_table_entry_get(
                &root,
                index,
                &table_physical_address
            )
        ) {
            return false;
        }

        if (table_physical_address == 0) {
            return false;
        }

        /*
         * First resolve only the common header. We need to
         * inspect the signature to identify MADT candidates.
         */
        if (
            !memory_physical_range_is_acpi(
                table_physical_address,
                ACPI_SDT_HEADER_SIZE
            )
        ) {
            return false;
        }

        const void *header_mapping;

        if (
            !direct_mapping_resolve_physical_range(
                table_physical_address,
                ACPI_SDT_HEADER_SIZE,
                &header_mapping
            )
        ) {
            return false;
        }

        if (
            !acpi_discovery_signature_equal(
                (const uint8_t *) header_mapping,
                "APIC"
            )
        ) {
            continue;
        }

        /*
         * Reject duplicate MADTs rather than selecting one
         * arbitrarily.
         */
        if (found) {
            return false;
        }

        const uint8_t *table_bytes;
        size_t table_size;

        if (
            !acpi_discovery_map_table(
                table_physical_address,
                &table_bytes,
                &table_size
            )
        ) {
            return false;
        }

        /*
         * acpi_madt_parse() validates the complete checksum,
         * fixed MADT header, and variable-length records.
         */
        if (
            !acpi_madt_parse(
                table_bytes,
                table_size,
                &candidate
            )
        ) {
            return false;
        }

        found = true;
    }

    if (!found) {
        return false;
    }

    *madt = candidate;

    return true;
}

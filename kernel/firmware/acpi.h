// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file acpi.h
 * @brief ACPI table discovery and validation primitives.
 */

#ifndef MYOS_FIRMWARE_ACPI_H
#define MYOS_FIRMWARE_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Size of the ACPI 1.0 RSDP.
 */
#define ACPI_RSDP_V1_SIZE 20U

/**
 * Size of the common ACPI System Description Table header.
 */
#define ACPI_SDT_HEADER_SIZE 36U

/**
 * Size of an ACPI table signature.
 */
#define ACPI_SDT_SIGNATURE_SIZE 4U

/**
 * Size of the MADT fixed header, including the common SDT header.
 */
#define ACPI_MADT_HEADER_SIZE \
    (ACPI_SDT_HEADER_SIZE + 8U)

/**
 * Minimum size of an ACPI 2.0 or later RSDP.
 */
#define ACPI_RSDP_V2_MIN_SIZE 36U

/**
 * Validated information decoded from an ACPI Root System
 * Description Pointer.
 *
 * Physical addresses are retained as numeric addresses only.
 * Parsing an RSDP does not map or dereference the referenced
 * RSDT or XSDT.
 */
struct acpi_rsdp_info {
    uint8_t revision;
    uint32_t length;

    uint32_t rsdt_physical_address;
    uint64_t xsdt_physical_address;
};

/**
 * Validated common header of an ACPI System Description Table.
 *
 * This structure contains only fields required by the generic
 * table-discovery layer. Table-specific payloads are decoded
 * separately.
 */
struct acpi_sdt_info {
    uint8_t signature[ACPI_SDT_SIGNATURE_SIZE];
    uint32_t length;
    uint8_t revision;
};

/**
 * ACPI root-table format.
 */
enum acpi_root_table_kind {
    ACPI_ROOT_TABLE_RSDT,
    ACPI_ROOT_TABLE_XSDT,
};

/**
 * Validated ACPI root table.
 *
 * The table retains a reference to the caller-provided byte sequence.
 * The caller must therefore keep those bytes accessible for the lifetime
 * of this structure.
 */
struct acpi_root_table {
    const uint8_t *data;
    size_t size;

    enum acpi_root_table_kind kind;

    uint32_t length;
    size_t entry_count;
};

/**
 * Validated ACPI Multiple APIC Description Table.
 *
 * The data pointer borrows the caller-provided table buffer.
 * No physical addresses are dereferenced by this parser.
 */
struct acpi_madt {
    const uint8_t *data;
    size_t size;

    uint32_t local_apic_address;
    uint32_t flags;

    size_t record_count;
};

/**
 * One variable-length MADT record.
 *
 * data points to the complete record, including Type and Length.
 */
struct acpi_madt_record {
    const uint8_t *data;
    uint8_t type;
    uint8_t length;
};

#define ACPI_MADT_RECORD_LOCAL_APIC          0U
#define ACPI_MADT_RECORD_IOAPIC              1U
#define ACPI_MADT_RECORD_INTERRUPT_OVERRIDE 2U
#define ACPI_MADT_RECORD_LOCAL_APIC_OVERRIDE 5U

struct acpi_madt_local_apic {
    uint8_t processor_uid;
    uint8_t apic_id;
    uint32_t flags;
};

struct acpi_madt_ioapic {
    uint8_t ioapic_id;
    uint32_t physical_address;
    uint32_t gsi_base;
};

struct acpi_madt_interrupt_override {
    uint8_t bus;
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
};

struct acpi_madt_local_apic_override {
    uint64_t physical_address;
};

/**
 * Validates and decodes an ACPI RSDP.
 *
 * ACPI 1.0 inputs must contain at least 20 bytes and pass the
 * legacy checksum.
 *
 * ACPI 2.0 and later inputs must additionally contain the
 * complete length declared by the RSDP and pass the extended
 * checksum.
 *
 * The output structure is cleared before validation. A failed
 * parse therefore never exposes partially decoded information.
 *
 * @param bytes RSDP byte sequence.
 * @param size Number of accessible bytes starting at bytes.
 * @param info Receives validated RSDP information.
 *
 * @return true when the RSDP is structurally valid and its
 *         required checksums pass; false otherwise.
 */
bool acpi_rsdp_parse(
    const uint8_t *bytes,
    size_t size,
    struct acpi_rsdp_info *info
);

/**
 * Reads and validates the Length field from a complete ACPI SDT header.
 *
 * This operation validates only the structural minimum needed to determine
 * the table size. It does not validate the table checksum.
 *
 * @param bytes Accessible SDT header bytes.
 * @param size Number of accessible bytes.
 * @param length Receives the declared complete table size.
 *
 * @return true when a complete common SDT header is present and declares a
 *         valid minimum length; false otherwise.
 */
bool acpi_sdt_length_read(
    const uint8_t *bytes,
    size_t size,
    uint32_t *length
);

/**
 * Validates and decodes the common ACPI SDT header.
 *
 * The caller must provide the complete table bytes described by
 * the Length field. The entire table checksum is validated before
 * any information is returned.
 *
 * @param bytes Complete ACPI table byte sequence.
 * @param size Number of accessible bytes starting at bytes.
 * @param info Receives validated common table information.
 *
 * @return true when the table is structurally valid and its
 *         checksum passes; false otherwise.
 */
bool acpi_sdt_parse(
    const uint8_t *bytes,
    size_t size,
    struct acpi_sdt_info *info
);

/**
 * Validates and decodes an ACPI RSDT or XSDT.
 *
 * The common SDT checksum and length are validated first. The table
 * signature then determines whether entries contain 32-bit or 64-bit
 * physical addresses.
 *
 * @param bytes Complete RSDT or XSDT byte sequence.
 * @param size Number of accessible bytes starting at bytes.
 * @param table Receives the validated root-table description.
 *
 * @return true for a structurally valid RSDT/XSDT; false otherwise.
 */
bool acpi_root_table_parse(
    const uint8_t *bytes,
    size_t size,
    struct acpi_root_table *table
);

/**
 * Returns one physical SDT address from a validated root table.
 *
 * RSDT entries are promoted from 32 bits to uint64_t. XSDT entries are
 * already 64-bit physical addresses.
 *
 * @param table Validated root table.
 * @param index Zero-based entry index.
 * @param physical_address Receives the physical address.
 *
 * @return true when the requested entry exists; false otherwise.
 */
bool acpi_root_table_entry_get(
    const struct acpi_root_table *table,
    size_t index,
    uint64_t *physical_address
);

/**
 * Validates and decodes a complete MADT.
 *
 * Validates the common SDT header and checksum, the APIC signature,
 * the fixed MADT header, and the complete sequence of variable-length
 * records. Unknown record types are retained without interpretation.
 *
 * @return true when the MADT is structurally valid; false otherwise.
 */
bool acpi_madt_parse(
    const uint8_t *bytes,
    size_t size,
    struct acpi_madt *madt
);

/**
 * Returns a variable-length record from a validated MADT.
 *
 * @return true when index identifies an existing record; false otherwise.
 */
bool acpi_madt_record_get(
    const struct acpi_madt *madt,
    size_t index,
    struct acpi_madt_record *record
);

/**
 * Decode supported MADT record types.
 *
 * Each function requires the exact record type and length.
 * A false return leaves the output unspecified.
 *
 * These accessors decode firmware fields but do not decide
 * whether the described interrupt topology is supported.
 */
bool acpi_madt_local_apic_decode(
    const struct acpi_madt_record *record,
    struct acpi_madt_local_apic *local_apic
);

bool acpi_madt_ioapic_decode(
    const struct acpi_madt_record *record,
    struct acpi_madt_ioapic *ioapic
);

bool acpi_madt_interrupt_override_decode(
    const struct acpi_madt_record *record,
    struct acpi_madt_interrupt_override *override
);

bool acpi_madt_local_apic_override_decode(
    const struct acpi_madt_record *record,
    struct acpi_madt_local_apic_override *override
);

/**
 * Resolves the effective Local APIC physical address declared by a MADT.
 *
 * A type-5 Local APIC Address Override replaces the 32-bit address in
 * the fixed MADT header. Multiple overrides are rejected.
 *
 * This function does not access Local APIC registers or validate the
 * processor's current APIC operating mode.
 *
 * @param madt Previously validated MADT.
 * @param physical_address Receives the effective physical address.
 *
 * @return true when an unambiguous address was obtained; false otherwise.
 */
bool acpi_madt_local_apic_address_get(
    const struct acpi_madt *madt,
    uint64_t *physical_address
);

#endif

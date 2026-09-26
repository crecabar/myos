// SPDX-License-Identifier: GPL-2.0-only

#include "acpi.h"

#include <stddef.h>
#include <stdint.h>

#define ACPI_RSDP_SIGNATURE_SIZE    8U

#define ACPI_RSDP_REVISION_OFFSET   15U
#define ACPI_RSDP_RSDT_OFFSET       16U

#define ACPI_RSDP_LENGTH_OFFSET     20U
#define ACPI_RSDP_XSDT_OFFSET       24U

#define ACPI_SDT_LENGTH_OFFSET      4U
#define ACPI_SDT_REVISION_OFFSET    8U

#define ACPI_ROOT_RSDT_ENTRY_SIZE   4U
#define ACPI_ROOT_XSDT_ENTRY_SIZE   8U

#define ACPI_MADT_LOCAL_APIC_OFFSET \
    ACPI_SDT_HEADER_SIZE

#define ACPI_MADT_FLAGS_OFFSET \
    (ACPI_SDT_HEADER_SIZE + 4U)

#define ACPI_MADT_RECORD_HEADER_SIZE 2U

static bool acpi_bytes_equal(
    const uint8_t *bytes,
    const char *string,
    size_t length
);

static bool acpi_checksum_valid(
    const uint8_t *bytes,
    size_t length
);

static uint16_t acpi_read_u16(
    const uint8_t *bytes
);

static uint32_t acpi_read_u32(
    const uint8_t *bytes
);

static uint64_t acpi_read_u64(
    const uint8_t *bytes
);

static void acpi_rsdp_info_clear(
    struct acpi_rsdp_info *info
);

static void acpi_sdt_info_clear(
    struct acpi_sdt_info *info
);

static void acpi_root_table_clear(
    struct acpi_root_table *table
);

static bool acpi_sdt_signature_equal(
    const struct acpi_sdt_info *info,
    const char *signature
);

static bool acpi_bytes_equal(
    const uint8_t *bytes,
    const char *string,
    size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        if (bytes[index] != (uint8_t) string[index]) {
            return false;
        }
    }

    return true;
}

static bool acpi_checksum_valid(
    const uint8_t *bytes,
    size_t length)
{
    uint8_t checksum = 0;

    for (size_t index = 0; index < length; ++index) {
        checksum =
            (uint8_t) (
                checksum +
                bytes[index]
            );
    }

    return checksum == 0;
}

static uint16_t acpi_read_u16(
    const uint8_t *bytes)
{
    return
        (uint16_t) bytes[0] |
        ((uint16_t) bytes[1] << 8);
}

static uint32_t acpi_read_u32(
    const uint8_t *bytes)
{
    return
        (uint32_t) bytes[0] |
        ((uint32_t) bytes[1] << 8) |
        ((uint32_t) bytes[2] << 16) |
        ((uint32_t) bytes[3] << 24);
}

static uint64_t acpi_read_u64(
    const uint8_t *bytes)
{
    return
        (uint64_t) bytes[0] |
        ((uint64_t) bytes[1] << 8) |
        ((uint64_t) bytes[2] << 16) |
        ((uint64_t) bytes[3] << 24) |
        ((uint64_t) bytes[4] << 32) |
        ((uint64_t) bytes[5] << 40) |
        ((uint64_t) bytes[6] << 48) |
        ((uint64_t) bytes[7] << 56);
}

static void acpi_rsdp_info_clear(
    struct acpi_rsdp_info *info)
{
    info->revision = 0;
    info->length = 0;
    info->rsdt_physical_address = 0;
    info->xsdt_physical_address = 0;
}

static void acpi_sdt_info_clear(
    struct acpi_sdt_info *info)
{
    for (size_t index = 0;
         index < ACPI_SDT_SIGNATURE_SIZE;
         ++index) {
        info->signature[index] = 0;
    }

    info->length = 0;
    info->revision = 0;
}

static void acpi_root_table_clear(
    struct acpi_root_table *table)
{
    table->data = NULL;
    table->size = 0;
    table->kind = ACPI_ROOT_TABLE_RSDT;
    table->length = 0;
    table->entry_count = 0;
}

static bool acpi_sdt_signature_equal(
    const struct acpi_sdt_info *info,
    const char *signature)
{
    if (info == NULL || signature == NULL) {
        return false;
    }

    for (size_t index = 0;
         index < ACPI_SDT_SIGNATURE_SIZE;
         ++index) {
        if (
            info->signature[index] !=
            (uint8_t) signature[index]
        ) {
            return false;
        }
    }

    return true;
}

bool acpi_rsdp_parse(
    const uint8_t *bytes,
    size_t size,
    struct acpi_rsdp_info *info)
{
    if (info == NULL) {
        return false;
    }

    acpi_rsdp_info_clear(info);

    if (bytes == NULL) {
        return false;
    }

    if (size < ACPI_RSDP_V1_SIZE) {
        return false;
    }

    if (
        !acpi_bytes_equal(
            bytes,
            "RSD PTR ",
            ACPI_RSDP_SIGNATURE_SIZE
        )
    ) {
        return false;
    }

    /*
     * The ACPI 1.0 checksum always covers the first
     * 20 bytes, including ACPI 2.0+ RSDPs.
     */
    if (
        !acpi_checksum_valid(
            bytes,
            ACPI_RSDP_V1_SIZE
        )
    ) {
        return false;
    }

    uint8_t revision =
        bytes[ACPI_RSDP_REVISION_OFFSET];

    uint32_t rsdt_physical_address =
        acpi_read_u32(
            &bytes[ACPI_RSDP_RSDT_OFFSET]
        );

    /*
     * ACPI 1.0 has no extended RSDP fields.
     *
     * Treat revisions below 2 as the legacy format.
     */
     if (revision == 0) {
        info->revision = revision;
        info->length = ACPI_RSDP_V1_SIZE;
        info->rsdt_physical_address =
            rsdt_physical_address;

        return true;
    }

    /*
     * Revision 1 is not a defined RSDP format.
     * Extended fields are valid starting at revision 2.
     */
    if (revision < 2) {
        return false;
    }

    if (size < ACPI_RSDP_V2_MIN_SIZE) {
        return false;
    }

    uint32_t length =
        acpi_read_u32(
            &bytes[ACPI_RSDP_LENGTH_OFFSET]
        );

    if (length < ACPI_RSDP_V2_MIN_SIZE) {
        return false;
    }

    /*
     * Never checksum bytes the caller has not established as
     * accessible.
     *
     * This also lets a future caller provide an RSDP longer
     * than the ACPI 2.0 minimum without changing the parser.
     */
    if ((size_t) length > size) {
        return false;
    }

    if (
        !acpi_checksum_valid(
            bytes,
            (size_t) length
        )
    ) {
        return false;
    }

    info->revision = revision;
    info->length = length;
    info->rsdt_physical_address =
        rsdt_physical_address;

    info->xsdt_physical_address =
        acpi_read_u64(
            &bytes[ACPI_RSDP_XSDT_OFFSET]
        );

    return true;
}

bool acpi_sdt_length_read(
    const uint8_t *bytes,
    size_t size,
    uint32_t *length)
{
    if (length == NULL) {
        return false;
    }

    *length = 0;

    if (bytes == NULL) {
        return false;
    }

    if (size < ACPI_SDT_HEADER_SIZE) {
        return false;
    }

    uint32_t declared_length =
        acpi_read_u32(
            &bytes[ACPI_SDT_LENGTH_OFFSET]
        );

    if (
        declared_length <
        ACPI_SDT_HEADER_SIZE
    ) {
        return false;
    }

    *length =
        declared_length;

    return true;
}

bool acpi_sdt_parse(
    const uint8_t *bytes,
    size_t size,
    struct acpi_sdt_info *info)
{
    if (info == NULL) {
        return false;
    }

    acpi_sdt_info_clear(info);

    if (bytes == NULL) {
        return false;
    }

    uint32_t length;

    if (
        !acpi_sdt_length_read(
            bytes,
            size,
            &length
        )
    ) {
        return false;
    }

    /*
     * Never checksum or inspect bytes that the caller has not
     * established as accessible.
     */
    if ((size_t) length > size) {
        return false;
    }

    if (
        !acpi_checksum_valid(
            bytes,
            (size_t) length
        )
    ) {
        return false;
    }

    for (size_t index = 0;
         index < ACPI_SDT_SIGNATURE_SIZE;
         ++index) {
        info->signature[index] =
            bytes[index];
    }

    info->length = length;
    info->revision =
        bytes[ACPI_SDT_REVISION_OFFSET];

    return true;
}

bool acpi_root_table_parse(
    const uint8_t *bytes,
    size_t size,
    struct acpi_root_table *table)
{
    if (table == NULL) {
        return false;
    }

    acpi_root_table_clear(table);

    if (bytes == NULL) {
        return false;
    }

    struct acpi_sdt_info info;

    if (
        !acpi_sdt_parse(
            bytes,
            size,
            &info
        )
    ) {
        return false;
    }

    enum acpi_root_table_kind kind;
    size_t entry_size;

    if (
        acpi_sdt_signature_equal(
            &info,
            "RSDT"
        )
    ) {
        kind = ACPI_ROOT_TABLE_RSDT;
        entry_size = ACPI_ROOT_RSDT_ENTRY_SIZE;
    } else if (
        acpi_sdt_signature_equal(
            &info,
            "XSDT"
        )
    ) {
        kind = ACPI_ROOT_TABLE_XSDT;
        entry_size = ACPI_ROOT_XSDT_ENTRY_SIZE;
    } else {
        return false;
    }

    size_t payload_size =
        (size_t) info.length -
        ACPI_SDT_HEADER_SIZE;

    if ((payload_size % entry_size) != 0) {
        return false;
    }

    table->data = bytes;
    table->size = (size_t) info.length;
    table->kind = kind;
    table->length = info.length;
    table->entry_count =
        payload_size / entry_size;

    return true;
}

bool acpi_root_table_entry_get(
    const struct acpi_root_table *table,
    size_t index,
    uint64_t *physical_address)
{
    if (
        table == NULL ||
        physical_address == NULL ||
        table->data == NULL
    ) {
        return false;
    }

    if (index >= table->entry_count) {
        return false;
    }

    size_t entry_size;

    switch (table->kind) {
        case ACPI_ROOT_TABLE_RSDT:
            entry_size =
                ACPI_ROOT_RSDT_ENTRY_SIZE;
            break;

        case ACPI_ROOT_TABLE_XSDT:
            entry_size =
                ACPI_ROOT_XSDT_ENTRY_SIZE;
            break;

        default:
            return false;
    }

    size_t offset =
        ACPI_SDT_HEADER_SIZE +
        (index * entry_size);

    if (
        offset > table->size ||
        entry_size > table->size - offset
    ) {
        return false;
    }

    if (
        table->kind ==
        ACPI_ROOT_TABLE_RSDT
    ) {
        *physical_address =
            (uint64_t)
            acpi_read_u32(
                &table->data[offset]
            );
    } else {
        *physical_address =
            acpi_read_u64(
                &table->data[offset]
            );
    }

    return true;
}

bool acpi_madt_parse(
    const uint8_t *bytes,
    size_t size,
    struct acpi_madt *madt)
{
    if (madt == NULL) {
        return false;
    }

    madt->data = NULL;
    madt->size = 0;
    madt->local_apic_address = 0;
    madt->flags = 0;
    madt->record_count = 0;

    if (bytes == NULL) {
        return false;
    }

    struct acpi_sdt_info info;

    if (
        !acpi_sdt_parse(
            bytes,
            size,
            &info
        )
    ) {
        return false;
    }

    if (
        !acpi_sdt_signature_equal(
            &info,
            "APIC"
        )
    ) {
        return false;
    }

    if (info.length < ACPI_MADT_HEADER_SIZE) {
        return false;
    }

    /*
     * Validate the entire record sequence before exposing the
     * MADT to callers.
     */
    size_t offset = ACPI_MADT_HEADER_SIZE;
    size_t record_count = 0;

    while (offset < info.length) {
        size_t remaining =
            (size_t) info.length - offset;

        if (
            remaining <
            ACPI_MADT_RECORD_HEADER_SIZE
        ) {
            return false;
        }

        uint8_t record_length =
            bytes[offset + 1U];

        if (
            record_length <
            ACPI_MADT_RECORD_HEADER_SIZE
        ) {
            return false;
        }

        if ((size_t) record_length > remaining) {
            return false;
        }

        offset += (size_t) record_length;
        ++record_count;
    }

    madt->data = bytes;
    madt->size = (size_t) info.length;

    madt->local_apic_address =
        acpi_read_u32(
            &bytes[ACPI_MADT_LOCAL_APIC_OFFSET]
        );

    madt->flags =
        acpi_read_u32(
            &bytes[ACPI_MADT_FLAGS_OFFSET]
        );

    madt->record_count = record_count;

    return true;
}

bool acpi_madt_record_get(
    const struct acpi_madt *madt,
    size_t index,
    struct acpi_madt_record *record)
{
    if (
        madt == NULL ||
        record == NULL ||
        madt->data == NULL ||
        index >= madt->record_count
    ) {
        return false;
    }

    size_t offset = ACPI_MADT_HEADER_SIZE;

    for (size_t current = 0;
         current <= index;
         ++current) {
        if (
            offset > madt->size ||
            madt->size - offset <
                ACPI_MADT_RECORD_HEADER_SIZE
        ) {
            return false;
        }

        const uint8_t *bytes =
            &madt->data[offset];

        uint8_t length = bytes[1];

        if (
            length <
                ACPI_MADT_RECORD_HEADER_SIZE ||
            (size_t) length >
                madt->size - offset
        ) {
            return false;
        }

        if (current == index) {
            record->data = bytes;
            record->type = bytes[0];
            record->length = length;

            return true;
        }

        offset += (size_t) length;
    }

    return false;
}

bool acpi_madt_local_apic_decode(
    const struct acpi_madt_record *record,
    struct acpi_madt_local_apic *local_apic)
{
    if (
        record == NULL ||
        local_apic == NULL ||
        record->data == NULL ||
        record->type != ACPI_MADT_RECORD_LOCAL_APIC ||
        record->length != 8U
    ) {
        return false;
    }

    const uint8_t *bytes = record->data;

    local_apic->processor_uid = bytes[2];
    local_apic->apic_id = bytes[3];
    local_apic->flags = acpi_read_u32(&bytes[4]);

    return true;
}

bool acpi_madt_ioapic_decode(
    const struct acpi_madt_record *record,
    struct acpi_madt_ioapic *ioapic)
{
    if (
        record == NULL ||
        ioapic == NULL ||
        record->data == NULL ||
        record->type != ACPI_MADT_RECORD_IOAPIC ||
        record->length != 12U
    ) {
        return false;
    }

    const uint8_t *bytes = record->data;

    ioapic->ioapic_id = bytes[2];
    ioapic->physical_address =
        acpi_read_u32(&bytes[4]);
    ioapic->gsi_base =
        acpi_read_u32(&bytes[8]);

    return true;
}

bool acpi_madt_interrupt_override_decode(
    const struct acpi_madt_record *record,
    struct acpi_madt_interrupt_override *override)
{
    if (
        record == NULL ||
        override == NULL ||
        record->data == NULL ||
        record->type !=
            ACPI_MADT_RECORD_INTERRUPT_OVERRIDE ||
        record->length != 10U
    ) {
        return false;
    }

    const uint8_t *bytes = record->data;

    override->bus = bytes[2];
    override->source_irq = bytes[3];
    override->gsi = acpi_read_u32(&bytes[4]);
    override->flags = acpi_read_u16(&bytes[8]);

    return true;
}

bool acpi_madt_local_apic_override_decode(
    const struct acpi_madt_record *record,
    struct acpi_madt_local_apic_override *override)
{
    if (
        record == NULL ||
        override == NULL ||
        record->data == NULL ||
        record->type !=
            ACPI_MADT_RECORD_LOCAL_APIC_OVERRIDE ||
        record->length != 12U
    ) {
        return false;
    }

    override->physical_address =
        acpi_read_u64(&record->data[4]);

    return true;
}

bool acpi_madt_local_apic_address_get(
    const struct acpi_madt *madt,
    uint64_t *physical_address)
{
    if (
        madt == NULL ||
        physical_address == NULL ||
        madt->data == NULL
    ) {
        return false;
    }

    uint64_t candidate =
        (uint64_t) madt->local_apic_address;

    bool override_found = false;

    for (
        size_t index = 0;
        index < madt->record_count;
        ++index
    ) {
        struct acpi_madt_record record;

        if (
            !acpi_madt_record_get(
                madt,
                index,
                &record
            )
        ) {
            return false;
        }

        if (
            record.type !=
            ACPI_MADT_RECORD_LOCAL_APIC_OVERRIDE
        ) {
            continue;
        }

        if (override_found) {
            return false;
        }

        struct acpi_madt_local_apic_override
            address_override;

        if (
            !acpi_madt_local_apic_override_decode(
                &record,
                &address_override
            )
        ) {
            return false;
        }

        candidate =
            address_override.physical_address;

        override_found = true;
    }

    if (candidate == 0) {
        return false;
    }

    *physical_address = candidate;

    return true;
}

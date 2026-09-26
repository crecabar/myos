// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file acpi_test.c
 * @brief ACPI parser regression tests.
 */

#include "acpi_test.h"

#include "../arch/x86_64/interrupt_topology.h"
#include "../arch/x86_64/lapic.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../firmware/acpi.h"
#include "../firmware/acpi_discovery.h"
#include "../memory/direct_mapping.h"
#include "../memory/memory.h"

#include <stddef.h>
#include <stdint.h>

#define ACPI_TEST_RSDT_PHYSICAL 0x12345000U
#define ACPI_TEST_XSDT_PHYSICAL 0x0000001234567000ULL

#define ACPI_TEST_SDT_SIZE \
    (ACPI_SDT_HEADER_SIZE + 4U)

#define ACPI_TEST_RSDT_ENTRY_COUNT 2U
#define ACPI_TEST_XSDT_ENTRY_COUNT 2U

#define ACPI_TEST_RSDT_SIZE \
    (ACPI_SDT_HEADER_SIZE + \
        ACPI_TEST_RSDT_ENTRY_COUNT * 4U)

#define ACPI_TEST_XSDT_SIZE \
    (ACPI_SDT_HEADER_SIZE + \
        ACPI_TEST_XSDT_ENTRY_COUNT * 8U)

#define ACPI_TEST_MADT_SIZE \
    (ACPI_MADT_HEADER_SIZE + 8U + 12U + 10U)

#define ACPI_TEST_MADT_LAPIC_ADDRESS 0xFEE00000U

#define ACPI_TEST_TABLE_0_PHYSICAL \
    0x0000000012345000ULL

#define ACPI_TEST_TABLE_1_PHYSICAL \
    0x00000000ABCDF000ULL

#define ACPI_TEST_SDT_CHECKSUM_OFFSET 9U

#define ACPI_TEST_CHECKSUM_OFFSET     8U
#define ACPI_TEST_REVISION_OFFSET     15U
#define ACPI_TEST_RSDT_OFFSET         16U
#define ACPI_TEST_LENGTH_OFFSET       20U
#define ACPI_TEST_XSDT_OFFSET         24U
#define ACPI_TEST_EXT_CHECKSUM_OFFSET 32U

static void acpi_test_write_u32(
    uint8_t *bytes,
    uint32_t value
);

static void acpi_test_write_u64(
    uint8_t *bytes,
    uint64_t value
);

static void acpi_test_set_checksum(
    uint8_t *bytes,
    size_t length,
    size_t checksum_offset
);

static void acpi_test_build_v1(
    uint8_t bytes[ACPI_RSDP_V1_SIZE]
);

static void acpi_test_build_v2(
    uint8_t bytes[ACPI_RSDP_V2_MIN_SIZE]
);

static void acpi_test_valid_v1(void);
static void acpi_test_valid_v2(void);
static void acpi_test_invalid_inputs(void);
static void acpi_test_invalid_checksums(void);
static void acpi_test_invalid_extended_lengths(void);
static void acpi_test_valid_sdt(void);
static void acpi_test_sdt_length(void);
static void acpi_test_invalid_sdt(void);
static void acpi_test_valid_rsdt(void);
static void acpi_test_valid_xsdt(void);
static void acpi_test_invalid_root_tables(void);
static void acpi_test_firmware_madt(
    const struct acpi_root_table *root
);
static void acpi_test_firmware_root_table(
    const struct acpi_rsdp_info *info
);
static void acpi_test_madt_parser(void);
static void acpi_test_madt_lapic_address(void);

static void acpi_test_write_u32(
    uint8_t *bytes,
    uint32_t value)
{
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8);
    bytes[2] = (uint8_t) (value >> 16);
    bytes[3] = (uint8_t) (value >> 24);
}

static void acpi_test_write_u64(
    uint8_t *bytes,
    uint64_t value)
{
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8);
    bytes[2] = (uint8_t) (value >> 16);
    bytes[3] = (uint8_t) (value >> 24);
    bytes[4] = (uint8_t) (value >> 32);
    bytes[5] = (uint8_t) (value >> 40);
    bytes[6] = (uint8_t) (value >> 48);
    bytes[7] = (uint8_t) (value >> 56);
}

static void acpi_test_set_checksum(
    uint8_t *bytes,
    size_t length,
    size_t checksum_offset)
{
    bytes[checksum_offset] = 0;

    uint8_t checksum = 0;

    for (size_t index = 0; index < length; ++index) {
        checksum =
            (uint8_t) (
                checksum +
                bytes[index]
            );
    }

    bytes[checksum_offset] =
        (uint8_t) (0U - checksum);
}

static void acpi_test_build_v1(
    uint8_t bytes[ACPI_RSDP_V1_SIZE])
{
    for (size_t index = 0;
         index < ACPI_RSDP_V1_SIZE;
         ++index) {
        bytes[index] = 0;
    }

    bytes[0] = 'R';
    bytes[1] = 'S';
    bytes[2] = 'D';
    bytes[3] = ' ';
    bytes[4] = 'P';
    bytes[5] = 'T';
    bytes[6] = 'R';
    bytes[7] = ' ';

    bytes[9] = 'M';
    bytes[10] = 'Y';
    bytes[11] = 'O';
    bytes[12] = 'S';
    bytes[13] = ' ';
    bytes[14] = ' ';

    bytes[ACPI_TEST_REVISION_OFFSET] = 0;

    acpi_test_write_u32(
        &bytes[ACPI_TEST_RSDT_OFFSET],
        ACPI_TEST_RSDT_PHYSICAL
    );

    acpi_test_set_checksum(
        bytes,
        ACPI_RSDP_V1_SIZE,
        ACPI_TEST_CHECKSUM_OFFSET
    );
}

static void acpi_test_build_v2(
    uint8_t bytes[ACPI_RSDP_V2_MIN_SIZE])
{
    for (size_t index = 0;
         index < ACPI_RSDP_V2_MIN_SIZE;
         ++index) {
        bytes[index] = 0;
    }

    bytes[0] = 'R';
    bytes[1] = 'S';
    bytes[2] = 'D';
    bytes[3] = ' ';
    bytes[4] = 'P';
    bytes[5] = 'T';
    bytes[6] = 'R';
    bytes[7] = ' ';

    bytes[9] = 'M';
    bytes[10] = 'Y';
    bytes[11] = 'O';
    bytes[12] = 'S';
    bytes[13] = ' ';
    bytes[14] = ' ';

    bytes[ACPI_TEST_REVISION_OFFSET] = 2;

    acpi_test_write_u32(
        &bytes[ACPI_TEST_RSDT_OFFSET],
        ACPI_TEST_RSDT_PHYSICAL
    );

    acpi_test_write_u32(
        &bytes[ACPI_TEST_LENGTH_OFFSET],
        ACPI_RSDP_V2_MIN_SIZE
    );

    acpi_test_write_u64(
        &bytes[ACPI_TEST_XSDT_OFFSET],
        ACPI_TEST_XSDT_PHYSICAL
    );

    /*
     * The legacy checksum remains mandatory for the first
     * 20 bytes of an extended RSDP.
     */
    acpi_test_set_checksum(
        bytes,
        ACPI_RSDP_V1_SIZE,
        ACPI_TEST_CHECKSUM_OFFSET
    );

    acpi_test_set_checksum(
        bytes,
        ACPI_RSDP_V2_MIN_SIZE,
        ACPI_TEST_EXT_CHECKSUM_OFFSET
    );
}

static void acpi_test_valid_v1(void)
{
    uint8_t bytes[ACPI_RSDP_V1_SIZE];

    acpi_test_build_v1(bytes);

    struct acpi_rsdp_info info;

    if (
        !acpi_rsdp_parse(
            bytes,
            sizeof(bytes),
            &info
        )
    ) {
        kernel_panic(
            "Valid ACPI 1.0 RSDP was rejected"
        );
    }

    if (
        info.revision != 0 ||
        info.length != ACPI_RSDP_V1_SIZE ||
        info.rsdt_physical_address !=
            ACPI_TEST_RSDT_PHYSICAL ||
        info.xsdt_physical_address != 0
    ) {
        kernel_panic(
            "ACPI 1.0 RSDP decoded incorrectly"
        );
    }
}

static void acpi_test_valid_v2(void)
{
    uint8_t bytes[ACPI_RSDP_V2_MIN_SIZE];

    acpi_test_build_v2(bytes);

    struct acpi_rsdp_info info;

    if (
        !acpi_rsdp_parse(
            bytes,
            sizeof(bytes),
            &info
        )
    ) {
        kernel_panic(
            "Valid ACPI 2.0 RSDP was rejected"
        );
    }

    if (
        info.revision != 2 ||
        info.length != ACPI_RSDP_V2_MIN_SIZE ||
        info.rsdt_physical_address !=
            ACPI_TEST_RSDT_PHYSICAL ||
        info.xsdt_physical_address !=
            ACPI_TEST_XSDT_PHYSICAL
    ) {
        kernel_panic(
            "ACPI 2.0 RSDP decoded incorrectly"
        );
    }
}

static void acpi_test_invalid_inputs(void)
{
    struct acpi_rsdp_info info;

    if (acpi_rsdp_parse(NULL, 0, &info)) {
        kernel_panic(
            "ACPI parser accepted NULL input"
        );
    }

    uint8_t bytes[ACPI_RSDP_V1_SIZE];

    acpi_test_build_v1(bytes);

    if (
        acpi_rsdp_parse(
            bytes,
            sizeof(bytes),
            NULL
        )
    ) {
        kernel_panic(
            "ACPI parser accepted NULL output"
        );
    }

    if (
        acpi_rsdp_parse(
            bytes,
            ACPI_RSDP_V1_SIZE - 1U,
            &info
        )
    ) {
        kernel_panic(
            "ACPI parser accepted truncated RSDP"
        );
    }

    acpi_test_build_v1(bytes);
    bytes[0] = 'X';

    if (
        acpi_rsdp_parse(
            bytes,
            sizeof(bytes),
            &info
        )
    ) {
        kernel_panic(
            "ACPI parser accepted invalid RSDP signature"
        );
    }

    acpi_test_build_v1(bytes);

    bytes[ACPI_TEST_REVISION_OFFSET] = 1;

    acpi_test_set_checksum(
        bytes,
        ACPI_RSDP_V1_SIZE,
        ACPI_TEST_CHECKSUM_OFFSET
    );

    if (
        acpi_rsdp_parse(
            bytes,
            sizeof(bytes),
            &info
        )
    ) {
        kernel_panic(
            "ACPI parser accepted undefined RSDP revision"
        );
    }
}

static void acpi_test_invalid_checksums(void)
{
    struct acpi_rsdp_info info;

    uint8_t v1[ACPI_RSDP_V1_SIZE];

    acpi_test_build_v1(v1);
    v1[9] ^= 0x01U;

    if (
        acpi_rsdp_parse(
            v1,
            sizeof(v1),
            &info
        )
    ) {
        kernel_panic(
            "ACPI parser accepted invalid legacy checksum"
        );
    }

    uint8_t v2[ACPI_RSDP_V2_MIN_SIZE];

    acpi_test_build_v2(v2);

    /*
     * Corrupt a byte outside the legacy 20-byte range so
     * only the extended checksum becomes invalid.
     */
    v2[33] ^= 0x01U;

    if (
        acpi_rsdp_parse(
            v2,
            sizeof(v2),
            &info
        )
    ) {
        kernel_panic(
            "ACPI parser accepted invalid extended checksum"
        );
    }
}

static void acpi_test_invalid_extended_lengths(void)
{
    struct acpi_rsdp_info info;
    uint8_t bytes[ACPI_RSDP_V2_MIN_SIZE];

    acpi_test_build_v2(bytes);

    if (
        acpi_rsdp_parse(
            bytes,
            ACPI_RSDP_V2_MIN_SIZE - 1U,
            &info
        )
    ) {
        kernel_panic(
            "ACPI parser accepted truncated extended RSDP"
        );
    }

    acpi_test_build_v2(bytes);

    acpi_test_write_u32(
        &bytes[ACPI_TEST_LENGTH_OFFSET],
        ACPI_RSDP_V2_MIN_SIZE - 1U
    );

    if (
        acpi_rsdp_parse(
            bytes,
            sizeof(bytes),
            &info
        )
    ) {
        kernel_panic(
            "ACPI parser accepted undersized extended length"
        );
    }

    acpi_test_build_v2(bytes);

    acpi_test_write_u32(
        &bytes[ACPI_TEST_LENGTH_OFFSET],
        ACPI_RSDP_V2_MIN_SIZE + 4U
    );

    if (
        acpi_rsdp_parse(
            bytes,
            sizeof(bytes),
            &info
        )
    ) {
        kernel_panic(
            "ACPI parser read beyond supplied RSDP bytes"
        );
    }
}

static void acpi_test_valid_sdt(void)
{
    uint8_t bytes[ACPI_TEST_SDT_SIZE];

    for (size_t index = 0;
         index < sizeof(bytes);
         ++index) {
        bytes[index] = 0;
    }

    bytes[0] = 'A';
    bytes[1] = 'P';
    bytes[2] = 'I';
    bytes[3] = 'C';

    acpi_test_write_u32(
        &bytes[4],
        (uint32_t) sizeof(bytes)
    );

    bytes[8] = 5;

    /*
     * Arbitrary payload. The generic SDT parser must include it
     * in the checksum without attempting to interpret it.
     */
    bytes[ACPI_SDT_HEADER_SIZE + 0U] = 0x11;
    bytes[ACPI_SDT_HEADER_SIZE + 1U] = 0x22;
    bytes[ACPI_SDT_HEADER_SIZE + 2U] = 0x33;
    bytes[ACPI_SDT_HEADER_SIZE + 3U] = 0x44;

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    struct acpi_sdt_info info;

    if (
        !acpi_sdt_parse(
            bytes,
            sizeof(bytes),
            &info
        )
    ) {
        kernel_panic(
            "Valid ACPI SDT was rejected"
        );
    }

    if (
        info.signature[0] != 'A' ||
        info.signature[1] != 'P' ||
        info.signature[2] != 'I' ||
        info.signature[3] != 'C' ||
        info.length != sizeof(bytes) ||
        info.revision != 5
    ) {
        kernel_panic(
            "ACPI SDT decoded incorrectly"
        );
    }
}

static void acpi_test_sdt_length(void)
{
    uint8_t bytes[ACPI_SDT_HEADER_SIZE] = {0};
    uint32_t length;

    acpi_test_write_u32(
        &bytes[4],
        ACPI_SDT_HEADER_SIZE
    );

    if (
        !acpi_sdt_length_read(
            bytes,
            sizeof(bytes),
            &length
        ) ||
        length != ACPI_SDT_HEADER_SIZE
    ) {
        kernel_panic(
            "ACPI SDT length reader rejected valid header"
        );
    }

    if (
        acpi_sdt_length_read(
            NULL,
            sizeof(bytes),
            &length
        )
    ) {
        kernel_panic(
            "ACPI SDT length reader accepted NULL input"
        );
    }

    if (
        acpi_sdt_length_read(
            bytes,
            sizeof(bytes),
            NULL
        )
    ) {
        kernel_panic(
            "ACPI SDT length reader accepted NULL output"
        );
    }

    if (
        acpi_sdt_length_read(
            bytes,
            ACPI_SDT_HEADER_SIZE - 1U,
            &length
        )
    ) {
        kernel_panic(
            "ACPI SDT length reader accepted truncated header"
        );
    }

    acpi_test_write_u32(
        &bytes[4],
        ACPI_SDT_HEADER_SIZE - 1U
    );

    if (
        acpi_sdt_length_read(
            bytes,
            sizeof(bytes),
            &length
        )
    ) {
        kernel_panic(
            "ACPI SDT length reader accepted undersized table"
        );
    }
}

static void acpi_test_invalid_sdt(void)
{
    struct acpi_sdt_info info;
    uint8_t bytes[ACPI_TEST_SDT_SIZE];

    if (acpi_sdt_parse(NULL, 0, &info)) {
        kernel_panic(
            "ACPI SDT parser accepted NULL input"
        );
    }

    for (size_t index = 0;
         index < sizeof(bytes);
         ++index) {
        bytes[index] = 0;
    }

    if (
        acpi_sdt_parse(
            bytes,
            ACPI_SDT_HEADER_SIZE - 1U,
            &info
        )
    ) {
        kernel_panic(
            "ACPI SDT parser accepted truncated header"
        );
    }

    /*
     * Declared table shorter than its mandatory header.
     */
    acpi_test_write_u32(
        &bytes[4],
        ACPI_SDT_HEADER_SIZE - 1U
    );

    if (
        acpi_sdt_parse(
            bytes,
            sizeof(bytes),
            &info
        )
    ) {
        kernel_panic(
            "ACPI SDT parser accepted undersized length"
        );
    }

    /*
     * Declared table extends beyond the supplied buffer.
     */
    acpi_test_write_u32(
        &bytes[4],
        (uint32_t) sizeof(bytes) + 1U
    );

    if (
        acpi_sdt_parse(
            bytes,
            sizeof(bytes),
            &info
        )
    ) {
        kernel_panic(
            "ACPI SDT parser read beyond supplied bytes"
        );
    }

    /*
     * Build an otherwise valid table and corrupt only its payload.
     */
    for (size_t index = 0;
         index < sizeof(bytes);
         ++index) {
        bytes[index] = 0;
    }

    bytes[0] = 'A';
    bytes[1] = 'P';
    bytes[2] = 'I';
    bytes[3] = 'C';

    acpi_test_write_u32(
        &bytes[4],
        (uint32_t) sizeof(bytes)
    );

    bytes[8] = 5;

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    bytes[ACPI_SDT_HEADER_SIZE] ^= 0x01U;

    if (
        acpi_sdt_parse(
            bytes,
            sizeof(bytes),
            &info
        )
    ) {
        kernel_panic(
            "ACPI SDT parser accepted invalid checksum"
        );
    }
}

static void acpi_test_valid_rsdt(void)
{
    uint8_t bytes[ACPI_TEST_RSDT_SIZE] = {0};

    bytes[0] = 'R';
    bytes[1] = 'S';
    bytes[2] = 'D';
    bytes[3] = 'T';

    acpi_test_write_u32(
        &bytes[4],
        (uint32_t) sizeof(bytes)
    );

    bytes[8] = 1;

    acpi_test_write_u32(
        &bytes[ACPI_SDT_HEADER_SIZE],
        (uint32_t) ACPI_TEST_TABLE_0_PHYSICAL
    );

    acpi_test_write_u32(
        &bytes[ACPI_SDT_HEADER_SIZE + 4U],
        (uint32_t) ACPI_TEST_TABLE_1_PHYSICAL
    );

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    struct acpi_root_table table;

    if (
        !acpi_root_table_parse(
            bytes,
            sizeof(bytes),
            &table
        )
    ) {
        kernel_panic(
            "Valid ACPI RSDT was rejected"
        );
    }

    if (
        table.kind != ACPI_ROOT_TABLE_RSDT ||
        table.entry_count !=
            ACPI_TEST_RSDT_ENTRY_COUNT
    ) {
        kernel_panic(
            "ACPI RSDT metadata decoded incorrectly"
        );
    }

    uint64_t physical_address;

    if (
        !acpi_root_table_entry_get(
            &table,
            0,
            &physical_address
        ) ||
        physical_address !=
            ACPI_TEST_TABLE_0_PHYSICAL
    ) {
        kernel_panic(
            "ACPI RSDT first entry decoded incorrectly"
        );
    }

    if (
        !acpi_root_table_entry_get(
            &table,
            1,
            &physical_address
        ) ||
        physical_address !=
            ACPI_TEST_TABLE_1_PHYSICAL
    ) {
        kernel_panic(
            "ACPI RSDT second entry decoded incorrectly"
        );
    }
}

static void acpi_test_valid_xsdt(void)
{
    uint8_t bytes[ACPI_TEST_XSDT_SIZE] = {0};

    bytes[0] = 'X';
    bytes[1] = 'S';
    bytes[2] = 'D';
    bytes[3] = 'T';

    acpi_test_write_u32(
        &bytes[4],
        (uint32_t) sizeof(bytes)
    );

    bytes[8] = 1;

    acpi_test_write_u64(
        &bytes[ACPI_SDT_HEADER_SIZE],
        ACPI_TEST_TABLE_0_PHYSICAL
    );

    acpi_test_write_u64(
        &bytes[ACPI_SDT_HEADER_SIZE + 8U],
        ACPI_TEST_TABLE_1_PHYSICAL
    );

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    struct acpi_root_table table;

    if (
        !acpi_root_table_parse(
            bytes,
            sizeof(bytes),
            &table
        )
    ) {
        kernel_panic(
            "Valid ACPI XSDT was rejected"
        );
    }

    if (
        table.kind != ACPI_ROOT_TABLE_XSDT ||
        table.entry_count !=
            ACPI_TEST_XSDT_ENTRY_COUNT
    ) {
        kernel_panic(
            "ACPI XSDT metadata decoded incorrectly"
        );
    }

    uint64_t physical_address;

    if (
        !acpi_root_table_entry_get(
            &table,
            0,
            &physical_address
        ) ||
        physical_address !=
            ACPI_TEST_TABLE_0_PHYSICAL
    ) {
        kernel_panic(
            "ACPI XSDT first entry decoded incorrectly"
        );
    }

    if (
        !acpi_root_table_entry_get(
            &table,
            1,
            &physical_address
        ) ||
        physical_address !=
            ACPI_TEST_TABLE_1_PHYSICAL
    ) {
        kernel_panic(
            "ACPI XSDT second entry decoded incorrectly"
        );
    }
}

static void acpi_test_invalid_root_tables(void)
{
    struct acpi_root_table table;
    uint64_t physical_address;

    uint8_t wrong_signature[
        ACPI_SDT_HEADER_SIZE
    ] = {0};

    wrong_signature[0] = 'A';
    wrong_signature[1] = 'P';
    wrong_signature[2] = 'I';
    wrong_signature[3] = 'C';

    acpi_test_write_u32(
        &wrong_signature[4],
        sizeof(wrong_signature)
    );

    acpi_test_set_checksum(
        wrong_signature,
        sizeof(wrong_signature),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    if (
        acpi_root_table_parse(
            wrong_signature,
            sizeof(wrong_signature),
            &table
        )
    ) {
        kernel_panic(
            "ACPI root parser accepted non-root SDT"
        );
    }

    /*
     * An XSDT payload must be an exact sequence of
     * eight-byte physical addresses.
     */
    uint8_t malformed_xsdt[
        ACPI_SDT_HEADER_SIZE + 4U
    ] = {0};

    malformed_xsdt[0] = 'X';
    malformed_xsdt[1] = 'S';
    malformed_xsdt[2] = 'D';
    malformed_xsdt[3] = 'T';

    acpi_test_write_u32(
        &malformed_xsdt[4],
        sizeof(malformed_xsdt)
    );

    acpi_test_set_checksum(
        malformed_xsdt,
        sizeof(malformed_xsdt),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    if (
        acpi_root_table_parse(
            malformed_xsdt,
            sizeof(malformed_xsdt),
            &table
        )
    ) {
        kernel_panic(
            "ACPI root parser accepted malformed XSDT payload"
        );
    }

    uint8_t valid[ACPI_TEST_XSDT_SIZE] = {0};

    valid[0] = 'X';
    valid[1] = 'S';
    valid[2] = 'D';
    valid[3] = 'T';

    acpi_test_write_u32(
        &valid[4],
        sizeof(valid)
    );

    acpi_test_set_checksum(
        valid,
        sizeof(valid),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    if (
        !acpi_root_table_parse(
            valid,
            sizeof(valid),
            &table
        )
    ) {
        kernel_panic(
            "Unable to build valid XSDT accessor fixture"
        );
    }

    if (
        acpi_root_table_entry_get(
            &table,
            table.entry_count,
            &physical_address
        )
    ) {
        kernel_panic(
            "ACPI root accessor accepted out-of-range index"
        );
    }

    if (
        acpi_root_table_entry_get(
            &table,
            0,
            NULL
        )
    ) {
        kernel_panic(
            "ACPI root accessor accepted NULL output"
        );
    }
}

static void acpi_test_firmware_madt(
    const struct acpi_root_table *root)
{
    if (root == NULL || root->data == NULL) {
        kernel_panic(
            "ACPI MADT discovery received invalid root table"
        );
    }

    bool madt_found = false;

    for (size_t index = 0;
         index < root->entry_count;
         ++index) {
        uint64_t physical_address;

        if (
            !acpi_root_table_entry_get(
                root,
                index,
                &physical_address
            )
        ) {
            kernel_panic(
                "Unable to read ACPI root-table entry"
            );
        }

        if (physical_address == 0) {
            kernel_panic(
                "ACPI root table contains null SDT address"
            );
        }

        /*
         * Establish ownership and mapping of the common header
         * before inspecting any bytes at this physical address.
         */
        if (
            !memory_physical_range_is_acpi(
                physical_address,
                ACPI_SDT_HEADER_SIZE
            )
        ) {
            kernel_panic(
                "ACPI SDT header lies outside ACPI memory"
            );
        }

        const void *header_mapping;

        if (
            !direct_mapping_resolve_physical_range(
                physical_address,
                ACPI_SDT_HEADER_SIZE,
                &header_mapping
            )
        ) {
            kernel_panic(
                "ACPI SDT header is not directly mapped"
            );
        }

        const uint8_t *header =
            (const uint8_t *) header_mapping;

        /*
         * The signature is inspected only to select a candidate.
         * It is not trusted until the complete selected table
         * passes its structural and checksum validation.
         */
        if (
            header[0] != 'A' ||
            header[1] != 'P' ||
            header[2] != 'I' ||
            header[3] != 'C'
        ) {
            continue;
        }

        if (madt_found) {
            kernel_panic(
                "ACPI root table contains multiple MADTs"
            );
        }

        uint32_t table_length;

        if (
            !acpi_sdt_length_read(
                header,
                ACPI_SDT_HEADER_SIZE,
                &table_length
            )
        ) {
            kernel_panic(
                "Firmware MADT declares invalid length"
            );
        }

        /*
         * A MADT contains the 36-byte common SDT header
         * followed by a 4-byte Local APIC address and
         * 4-byte flags field.
         */
        if (
            table_length <
            ACPI_SDT_HEADER_SIZE + 8U
        ) {
            kernel_panic(
                "Firmware MADT is shorter than its fixed header"
            );
        }

        if (
            !memory_physical_range_is_acpi(
                physical_address,
                (size_t) table_length
            )
        ) {
            kernel_panic(
                "Firmware MADT extends outside ACPI memory"
            );
        }

        const void *table_mapping;

        if (
            !direct_mapping_resolve_physical_range(
                physical_address,
                (size_t) table_length,
                &table_mapping
            )
        ) {
            kernel_panic(
                "Firmware MADT is not completely mapped"
            );
        }

        struct acpi_sdt_info madt_info;

        if (
            !acpi_sdt_parse(
                (const uint8_t *) table_mapping,
                (size_t) table_length,
                &madt_info
            )
        ) {
            kernel_panic(
                "Firmware MADT checksum or header validation failed"
            );
        }

        struct acpi_madt madt;

        if (
            !acpi_madt_parse(
                (const uint8_t *) table_mapping,
                (size_t) table_length,
                &madt
            )
        ) {
            kernel_panic(
                "Firmware MADT record validation failed"
            );
        }

        diagnostics_printf(
            "[acpi] MADT Local APIC=%x flags=%x records=%u\n",
            (uint64_t) madt.local_apic_address,
            (uint64_t) madt.flags,
            (uint64_t) madt.record_count
        );

        struct interrupt_topology discovered_topology;

        if (
            !interrupt_topology_from_madt(
                &madt,
                &discovered_topology
            )
        ) {
            kernel_panic(
                "Unable to build firmware interrupt topology"
            );
        }

        diagnostics_printf(
            "[acpi] Discovered topology: IOAPIC=%x "
            "GSI-base=%u PIT-IRQ=%u PIT-GSI=%u "
            "active-low=%u level=%u\n",
            discovered_topology.ioapic_physical_address,
            (uint64_t) discovered_topology.ioapic_gsi_base,
            (uint64_t) discovered_topology.timer_route.irq,
            (uint64_t) discovered_topology.timer_route.gsi,
            (uint64_t) discovered_topology.timer_route.active_low,
            (uint64_t) discovered_topology.timer_route.level_triggered
        );

        if (
            discovered_topology.ioapic_physical_address !=
                0xFEC00000ULL ||
            discovered_topology.ioapic_gsi_base != 0 ||
            discovered_topology.timer_route.irq != 0 ||
            discovered_topology.timer_route.gsi != 2 ||
            discovered_topology.timer_route.active_low ||
            discovered_topology.timer_route.level_triggered
        ) {
            kernel_panic(
                "Firmware topology differs from Q35 test expectations"
            );
        }

        for (size_t record_index = 0;
             record_index < madt.record_count;
             ++record_index) {
            struct acpi_madt_record record;

            if (
                !acpi_madt_record_get(
                    &madt,
                    record_index,
                    &record
                )
            ) {
                kernel_panic(
                    "Unable to read firmware MADT record"
                );
            }

            diagnostics_printf(
                "[acpi] MADT record %u: type=%u length=%u\n",
                (uint64_t) record_index,
                (uint64_t) record.type,
                (uint64_t) record.length
            );

            switch (record.type) {
                case ACPI_MADT_RECORD_LOCAL_APIC: {
                    struct acpi_madt_local_apic local_apic;

                    if (
                        !acpi_madt_local_apic_decode(
                            &record,
                            &local_apic
                        )
                    ) {
                        kernel_panic(
                            "Invalid firmware Local APIC record"
                        );
                    }

                    diagnostics_printf(
                        "[acpi]   LAPIC: UID=%u ID=%u flags=%x\n",
                        (uint64_t) local_apic.processor_uid,
                        (uint64_t) local_apic.apic_id,
                        (uint64_t) local_apic.flags
                    );

                    break;
                }

                case ACPI_MADT_RECORD_IOAPIC: {
                    struct acpi_madt_ioapic ioapic;

                    if (
                        !acpi_madt_ioapic_decode(
                            &record,
                            &ioapic
                        )
                    ) {
                        kernel_panic(
                            "Invalid firmware IOAPIC record"
                        );
                    }

                    diagnostics_printf(
                        "[acpi]   IOAPIC: ID=%u address=%x GSI-base=%u\n",
                        (uint64_t) ioapic.ioapic_id,
                        (uint64_t) ioapic.physical_address,
                        (uint64_t) ioapic.gsi_base
                    );

                    break;
                }

                case ACPI_MADT_RECORD_INTERRUPT_OVERRIDE: {
                    struct acpi_madt_interrupt_override
                        irq_override;

                    if (
                        !acpi_madt_interrupt_override_decode(
                            &record,
                            &irq_override
                        )
                    ) {
                        kernel_panic(
                            "Invalid firmware interrupt override"
                        );
                    }

                    diagnostics_printf(
                        "[acpi]   ISO: bus=%u IRQ=%u GSI=%u flags=%x\n",
                        (uint64_t) irq_override.bus,
                        (uint64_t) irq_override.source_irq,
                        (uint64_t) irq_override.gsi,
                        (uint64_t) irq_override.flags
                    );

                    break;
                }

                case ACPI_MADT_RECORD_LOCAL_APIC_OVERRIDE: {
                    struct acpi_madt_local_apic_override
                        local_apic_override;

                    if (
                        !acpi_madt_local_apic_override_decode(
                            &record,
                            &local_apic_override
                        )
                    ) {
                        kernel_panic(
                            "Invalid firmware Local APIC override"
                        );
                    }

                    diagnostics_printf(
                        "[acpi]   LAPIC address override=%x\n",
                        local_apic_override.physical_address
                    );

                    break;
                }

                default:
                    /*
                     * Other MADT record types are not used by
                     * the current interrupt-topology discovery.
                     */
                    break;
            }
        }

        if (
            madt_info.signature[0] != 'A' ||
            madt_info.signature[1] != 'P' ||
            madt_info.signature[2] != 'I' ||
            madt_info.signature[3] != 'C'
        ) {
            kernel_panic(
                "Validated MADT has unexpected signature"
            );
        }

        diagnostics_printf(
            "[acpi] Firmware MADT validated: "
            "physical=%x length=%u revision=%u\n",
            physical_address,
            (uint64_t) madt_info.length,
            (uint64_t) madt_info.revision
        );

        madt_found = true;
    }

    if (!madt_found) {
        kernel_panic(
            "Firmware ACPI root table contains no MADT"
        );
    }

    diagnostics_write(
        "[acpi] Firmware MADT discovery test passed\n"
    );
}

static void acpi_test_firmware_root_table(
    const struct acpi_rsdp_info *info)
{
    if (info == NULL) {
        kernel_panic(
            "ACPI firmware root-table test received NULL info"
        );
    }

    uint64_t root_physical_address;
    enum acpi_root_table_kind expected_kind;

    /*
     * Prefer the XSDT whenever ACPI 2.0+ supplies one.
     */
    if (
        info->revision >= 2 &&
        info->xsdt_physical_address != 0
    ) {
        root_physical_address =
            info->xsdt_physical_address;

        expected_kind =
            ACPI_ROOT_TABLE_XSDT;
    } else {
        root_physical_address =
            (uint64_t)
            info->rsdt_physical_address;

        expected_kind =
            ACPI_ROOT_TABLE_RSDT;
    }

    if (root_physical_address == 0) {
        kernel_panic(
            "Firmware RSDP provides no root ACPI table"
        );
    }

    /*
     * Establish ownership of the common header before mapping or
     * inspecting any byte from it.
     */
    if (
        !memory_physical_range_is_acpi(
            root_physical_address,
            ACPI_SDT_HEADER_SIZE
        )
    ) {
        kernel_panic(
            "Firmware ACPI root header lies outside ACPI memory"
        );
    }

    const void *header_mapping;

    if (
        !direct_mapping_resolve_physical_range(
            root_physical_address,
            ACPI_SDT_HEADER_SIZE,
            &header_mapping
        )
    ) {
        kernel_panic(
            "Firmware ACPI root header is not directly mapped"
        );
    }

    uint32_t table_length;

    if (
        !acpi_sdt_length_read(
            (const uint8_t *) header_mapping,
            ACPI_SDT_HEADER_SIZE,
            &table_length
        )
    ) {
        kernel_panic(
            "Firmware ACPI root header has invalid length"
        );
    }

    /*
     * Length came from firmware. Do not use it to read the full
     * table until both physical ownership and mapping coverage have
     * independently accepted the complete interval.
     */
    if (
        !memory_physical_range_is_acpi(
            root_physical_address,
            (size_t) table_length
        )
    ) {
        kernel_panic(
            "Firmware ACPI root table leaves ACPI memory"
        );
    }

    const void *table_mapping;

    if (
        !direct_mapping_resolve_physical_range(
            root_physical_address,
            (size_t) table_length,
            &table_mapping
        )
    ) {
        kernel_panic(
            "Firmware ACPI root table is not completely mapped"
        );
    }

    struct acpi_root_table table;

    if (
        !acpi_root_table_parse(
            (const uint8_t *) table_mapping,
            (size_t) table_length,
            &table
        )
    ) {
        kernel_panic(
            "Firmware ACPI root table validation failed"
        );
    }

    if (table.kind != expected_kind) {
        kernel_panic(
            "Firmware ACPI root table kind differs from RSDP"
        );
    }

    if (table.entry_count == 0) {
        kernel_panic(
            "Firmware ACPI root table contains no SDT entries"
        );
    }

    diagnostics_printf(
        "[acpi] Firmware %s validated: %u entries, %u bytes\n",
        table.kind == ACPI_ROOT_TABLE_XSDT
            ? "XSDT"
            : "RSDT",
        (uint64_t) table.entry_count,
        (uint64_t) table.length
    );

    acpi_test_firmware_madt(
        &table
    );

    /*
     * Preserve the basic rejection contracts established by the
     * previous physical-range test.
     */
    if (
        memory_physical_range_is_acpi(
            root_physical_address,
            0
        )
    ) {
        kernel_panic(
            "ACPI physical range accepted zero size"
        );
    }

    if (
        memory_physical_range_is_acpi(
            UINT64_MAX - 1U,
            4U
        )
    ) {
        kernel_panic(
            "ACPI physical range accepted overflowing interval"
        );
    }
}

static void acpi_test_madt_parser(void)
{
    uint8_t bytes[ACPI_TEST_MADT_SIZE] = {0};

    bytes[0] = 'A';
    bytes[1] = 'P';
    bytes[2] = 'I';
    bytes[3] = 'C';

    acpi_test_write_u32(
        &bytes[4],
        (uint32_t) sizeof(bytes)
    );

    bytes[8] = 3;

    acpi_test_write_u32(
        &bytes[ACPI_SDT_HEADER_SIZE],
        ACPI_TEST_MADT_LAPIC_ADDRESS
    );

    /*
     * Three records with the standard lengths of:
     * Local APIC (type 0), IOAPIC (type 1), and
     * Interrupt Source Override (type 2).
     *
     * Their payloads are not interpreted in this increment.
     */
    size_t offset = ACPI_MADT_HEADER_SIZE;

    /* Local APIC: processor UID 0, APIC ID 1, enabled. */
    bytes[offset] = ACPI_MADT_RECORD_LOCAL_APIC;
    bytes[offset + 1U] = 8;
    bytes[offset + 2U] = 0;
    bytes[offset + 3U] = 1;

    acpi_test_write_u32(
        &bytes[offset + 4U],
        1U
    );

    offset += 8U;

    /* IOAPIC: ID 2, physical base 0xFEC00000, GSI base 0. */
    bytes[offset] = ACPI_MADT_RECORD_IOAPIC;
    bytes[offset + 1U] = 12;
    bytes[offset + 2U] = 2;
    bytes[offset + 3U] = 0;

    acpi_test_write_u32(
        &bytes[offset + 4U],
        0xFEC00000U
    );

    acpi_test_write_u32(
        &bytes[offset + 8U],
        0U
    );

    offset += 12U;

    /* ISA IRQ0 is overridden to GSI 2. */
    bytes[offset] = ACPI_MADT_RECORD_INTERRUPT_OVERRIDE;
    bytes[offset + 1U] = 10;
    bytes[offset + 2U] = 0;
    bytes[offset + 3U] = 0;

    acpi_test_write_u32(
        &bytes[offset + 4U],
        2U
    );

    bytes[offset + 8U] = 0;
    bytes[offset + 9U] = 0;

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    struct acpi_madt madt;

    if (
        !acpi_madt_parse(
            bytes,
            sizeof(bytes),
            &madt
        )
    ) {
        kernel_panic(
            "Valid synthetic MADT was rejected"
        );
    }

    if (
        madt.local_apic_address !=
            ACPI_TEST_MADT_LAPIC_ADDRESS ||
        madt.record_count != 3
    ) {
        kernel_panic(
            "Synthetic MADT decoded incorrectly"
        );
    }

    const uint8_t expected_types[3] = {
        0, 1, 2
    };

    const uint8_t expected_lengths[3] = {
        8, 12, 10
    };

    for (size_t index = 0; index < 3; ++index) {
        struct acpi_madt_record record;

        if (
            !acpi_madt_record_get(
                &madt,
                index,
                &record
            )
        ) {
            kernel_panic(
                "Unable to read synthetic MADT record"
            );
        }

        if (
            record.type != expected_types[index] ||
            record.length != expected_lengths[index]
        ) {
            kernel_panic(
                "Synthetic MADT record decoded incorrectly"
            );
        }
    }

    struct acpi_madt_record local_apic_record;
    struct acpi_madt_record ioapic_record;
    struct acpi_madt_record override_record;

    if (
        !acpi_madt_record_get(
            &madt, 0, &local_apic_record
        ) ||
        !acpi_madt_record_get(
            &madt, 1, &ioapic_record
        ) ||
        !acpi_madt_record_get(
            &madt, 2, &override_record
        )
    ) {
        kernel_panic(
            "Unable to retrieve synthetic MADT records"
        );
    }

    struct acpi_madt_local_apic local_apic;

    if (
        !acpi_madt_local_apic_decode(
            &local_apic_record,
            &local_apic
        ) ||
        local_apic.processor_uid != 0 ||
        local_apic.apic_id != 1 ||
        local_apic.flags != 1
    ) {
        kernel_panic(
            "Synthetic Local APIC decoded incorrectly"
        );
    }

    struct acpi_madt_ioapic ioapic;

    if (
        !acpi_madt_ioapic_decode(
            &ioapic_record,
            &ioapic
        ) ||
        ioapic.ioapic_id != 2 ||
        ioapic.physical_address != 0xFEC00000U ||
        ioapic.gsi_base != 0
    ) {
        kernel_panic(
            "Synthetic IOAPIC decoded incorrectly"
        );
    }

    struct acpi_madt_interrupt_override irq_override;

    if (
        !acpi_madt_interrupt_override_decode(
            &override_record,
            &irq_override
        ) ||
        irq_override.bus != 0 ||
        irq_override.source_irq != 0 ||
        irq_override.gsi != 2 ||
        irq_override.flags != 0
    ) {
        kernel_panic(
            "Synthetic IRQ override decoded incorrectly"
        );
    }

    struct interrupt_topology topology;

    if (
        !interrupt_topology_from_madt(
            &madt,
            &topology
        ) ||
        topology.ioapic_physical_address != 0xFEC00000ULL ||
        topology.ioapic_gsi_base != 0 ||
        topology.timer_route.irq != 0 ||
        topology.timer_route.gsi != 2 ||
        topology.timer_route.active_low ||
        topology.timer_route.level_triggered
    ) {
        kernel_panic(
            "Synthetic Q35 interrupt topology decoded incorrectly"
        );
    }

    struct interrupt_route keyboard_route;

    if (
        !interrupt_topology_isa_route_from_madt(
            &madt,
            1U,
            &keyboard_route
        ) ||
        keyboard_route.irq != 1U ||
        keyboard_route.gsi != 1U ||
        keyboard_route.active_low ||
        keyboard_route.level_triggered
    ) {
        kernel_panic(
            "Synthetic ISA keyboard route decoded incorrectly"
        );
    }

    struct interrupt_route mouse_route;

    if (
        !interrupt_topology_isa_route_from_madt(
            &madt,
            12U,
            &mouse_route
        ) ||
        mouse_route.irq != 12U ||
        mouse_route.gsi != 12U ||
        mouse_route.active_low ||
        mouse_route.level_triggered
    ) {
        kernel_panic(
            "Synthetic ISA mouse route decoded incorrectly"
        );
    }

    /*
     * Replace the fixture with a different valid topology:
     *
     *   IOAPIC GSI base = 4
     *   ISA IRQ0 -> GSI 7
     *   Active-low, level-triggered
     */
    size_t ioapic_offset =
        ACPI_MADT_HEADER_SIZE + 8U;

    size_t iso_offset =
        ioapic_offset + 12U;

    acpi_test_write_u32(
        &bytes[ioapic_offset + 8U],
        4U
    );

    acpi_test_write_u32(
        &bytes[iso_offset + 4U],
        7U
    );

    bytes[iso_offset + 8U] = 0x0FU;
    bytes[iso_offset + 9U] = 0;

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    if (
        !acpi_madt_parse(
            bytes,
            sizeof(bytes),
            &madt
        ) ||
        !interrupt_topology_from_madt(
            &madt,
            &topology
        )
    ) {
        kernel_panic(
            "Synthetic alternate interrupt topology was rejected"
        );
    }

    if (
        topology.ioapic_gsi_base != 4 ||
        topology.timer_route.gsi != 7 ||
        !topology.timer_route.active_low ||
        !topology.timer_route.level_triggered
    ) {
        kernel_panic(
            "Synthetic alternate interrupt topology decoded incorrectly"
        );
    }

    /*
     * Polarity encoding 2 is reserved. Keep the MADT checksum
     * valid so the topology builder must reject the ISO itself.
     */
    bytes[iso_offset + 8U] = 0x02U;

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    if (
        !acpi_madt_parse(
            bytes,
            sizeof(bytes),
            &madt
        )
    ) {
        kernel_panic(
            "Unable to prepare invalid-ISO topology fixture"
        );
    }

    if (
        interrupt_topology_from_madt(
            &madt,
            &topology
        )
    ) {
        kernel_panic(
            "Interrupt topology accepted reserved ISO polarity"
        );
    }

    struct acpi_madt_record record;

    if (
        acpi_madt_record_get(
            &madt,
            madt.record_count,
            &record
        )
    ) {
        kernel_panic(
            "MADT accessor accepted out-of-range index"
        );
    }

    /*
     * Corrupt the first record length. Recalculate the SDT
     * checksum so the rejection must come from record validation.
     */
    bytes[ACPI_MADT_HEADER_SIZE + 1U] = 0;

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    if (
        acpi_madt_parse(
            bytes,
            sizeof(bytes),
            &madt
        )
    ) {
        kernel_panic(
            "MADT parser accepted zero-length record"
        );
    }

    bytes[ACPI_MADT_HEADER_SIZE + 1U] = 1;

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    if (
        acpi_madt_parse(
            bytes,
            sizeof(bytes),
            &madt
        )
    ) {
        kernel_panic(
            "MADT parser accepted undersized record"
        );
    }

    bytes[ACPI_MADT_HEADER_SIZE + 1U] = 255;

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    if (
        acpi_madt_parse(
            bytes,
            sizeof(bytes),
            &madt
        )
    ) {
        kernel_panic(
            "MADT parser accepted record beyond table end"
        );
    }

    uint8_t local_apic_override_bytes[12] = {
        ACPI_MADT_RECORD_LOCAL_APIC_OVERRIDE,
        12U,
        0U,
        0U
    };

    acpi_test_write_u64(
        &local_apic_override_bytes[4],
        0x00000001FEE00000ULL
    );

    struct acpi_madt_record local_apic_override_record = {
        .data = local_apic_override_bytes,
        .type = ACPI_MADT_RECORD_LOCAL_APIC_OVERRIDE,
        .length = 12U,
    };

    struct acpi_madt_local_apic_override local_apic_override;

    if (
        !acpi_madt_local_apic_override_decode(
            &local_apic_override_record,
            &local_apic_override
        ) ||
        local_apic_override.physical_address !=
            0x00000001FEE00000ULL
    ) {
        kernel_panic(
            "Synthetic Local APIC override decoded incorrectly"
        );
    }
}

static void acpi_test_madt_lapic_address(void)
{
    uint8_t bytes[
        ACPI_MADT_HEADER_SIZE + 12U
    ] = {0};

    bytes[0] = 'A';
    bytes[1] = 'P';
    bytes[2] = 'I';
    bytes[3] = 'C';

    acpi_test_write_u32(
        &bytes[4],
        (uint32_t) sizeof(bytes)
    );

    bytes[8] = 3;

    acpi_test_write_u32(
        &bytes[ACPI_SDT_HEADER_SIZE],
        0xFEE00000U
    );

    size_t offset =
        ACPI_MADT_HEADER_SIZE;

    bytes[offset] =
        ACPI_MADT_RECORD_LOCAL_APIC_OVERRIDE;

    bytes[offset + 1U] = 12U;

    acpi_test_write_u64(
        &bytes[offset + 4U],
        0x00000001FEE00000ULL
    );

    acpi_test_set_checksum(
        bytes,
        sizeof(bytes),
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    struct acpi_madt madt;

    if (
        !acpi_madt_parse(
            bytes,
            sizeof(bytes),
            &madt
        )
    ) {
        kernel_panic(
            "Valid MADT LAPIC override fixture was rejected"
        );
    }

    uint64_t physical_address;

    if (
        !acpi_madt_local_apic_address_get(
            &madt,
            &physical_address
        ) ||
        physical_address !=
            0x00000001FEE00000ULL
    ) {
        kernel_panic(
            "MADT Local APIC address override decoded incorrectly"
        );
    }

    /*
     * Remove the override and verify that the fixed
     * MADT header becomes authoritative again.
     */
    acpi_test_write_u32(
        &bytes[4],
        ACPI_MADT_HEADER_SIZE
    );

    acpi_test_set_checksum(
        bytes,
        ACPI_MADT_HEADER_SIZE,
        ACPI_TEST_SDT_CHECKSUM_OFFSET
    );

    if (
        !acpi_madt_parse(
            bytes,
            ACPI_MADT_HEADER_SIZE,
            &madt
        ) ||
        !acpi_madt_local_apic_address_get(
            &madt,
            &physical_address
        ) ||
        physical_address != 0xFEE00000ULL
    ) {
        kernel_panic(
            "MADT fixed Local APIC address decoded incorrectly"
        );
    }
}

void acpi_test_run(
    const uint8_t *firmware_rsdp,
    size_t firmware_rsdp_size)
{
    acpi_test_valid_v1();
    acpi_test_valid_v2();
    acpi_test_invalid_inputs();
    acpi_test_invalid_checksums();
    acpi_test_invalid_extended_lengths();

    diagnostics_write(
        "[acpi] RSDP parser regression tests passed\n"
    );

    acpi_test_sdt_length();
    acpi_test_valid_sdt();
    acpi_test_invalid_sdt();

    diagnostics_write(
        "[acpi] SDT header regression tests passed\n"
    );

    acpi_test_valid_rsdt();
    acpi_test_valid_xsdt();
    acpi_test_invalid_root_tables();

    diagnostics_write(
        "[acpi] RSDT/XSDT regression tests passed\n"
    );

    acpi_test_madt_parser();

    diagnostics_write(
        "[acpi] MADT record parser regression tests passed\n"
    );

    /*
     * Finally validate the snapshot actually supplied by the
     * firmware through Limine. This joins the pure parser tests
     * to the real boot path without making production startup
     * depend on ACPI discovery yet.
     */
    struct acpi_rsdp_info firmware_info;

    if (
        !acpi_rsdp_parse(
            firmware_rsdp,
            firmware_rsdp_size,
            &firmware_info
        )
    ) {
        kernel_panic(
            "Firmware RSDP snapshot validation failed"
        );
    }

    diagnostics_write(
        "[acpi] Firmware RSDP snapshot test passed\n"
    );

    acpi_test_firmware_root_table(
        &firmware_info
    );

    diagnostics_write(
        "[acpi] Firmware root-table discovery test passed\n"
    );

    /*
     * Exercise the same discovery entry point that production
     * initialization will use.
     */
    struct acpi_madt discovered_madt;

    if (
        !acpi_madt_discover(
            firmware_rsdp,
            firmware_rsdp_size,
            &discovered_madt
        )
    ) {
        kernel_panic(
            "Production ACPI MADT discovery failed"
        );
    }

    uint64_t firmware_lapic_physical;

    if (
        !acpi_madt_local_apic_address_get(
            &discovered_madt,
            &firmware_lapic_physical
        )
    ) {
        kernel_panic(
            "Unable to obtain firmware Local APIC address"
        );
    }

    uint64_t active_lapic_physical;
    uint64_t active_lapic_virtual;

    if (
        !lapic_mapping_info(
            &active_lapic_physical,
            &active_lapic_virtual
        )
    ) {
        kernel_panic(
            "Unable to inspect initialized Local APIC mapping"
        );
    }

    if (
        firmware_lapic_physical !=
        active_lapic_physical
    ) {
        kernel_panic(
            "Firmware Local APIC address differs from active LAPIC"
        );
    }

    diagnostics_printf(
        "[acpi] Local APIC address verified: "
        "firmware=%x active=%x\n",
        firmware_lapic_physical,
        active_lapic_physical
    );

    acpi_test_madt_lapic_address();

    diagnostics_write(
        "[acpi] MADT Local APIC address regression tests passed\n"
    );

    struct interrupt_topology discovered_topology;

    if (
        !interrupt_topology_from_madt(
            &discovered_madt,
            &discovered_topology
        )
    ) {
        kernel_panic(
            "Production MADT did not yield an interrupt topology"
        );
    }

    /*
     * Q35 regression expectations. These constants belong to
     * the test, not to the production discovery implementation.
     */
    if (
        discovered_madt.local_apic_address !=
            0xFEE00000U ||
        discovered_topology.ioapic_physical_address !=
            0xFEC00000ULL ||
        discovered_topology.ioapic_gsi_base != 0 ||
        discovered_topology.timer_route.irq != 0 ||
        discovered_topology.timer_route.gsi != 2 ||
        discovered_topology.timer_route.active_low ||
        discovered_topology.timer_route.level_triggered
    ) {
        kernel_panic(
            "Production ACPI discovery differs from Q35 expectations"
        );
    }

    diagnostics_write(
        "[acpi] Production MADT discovery regression test passed\n"
    );
}

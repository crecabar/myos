// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file acpi_test.c
 * @brief ACPI parser regression tests.
 */

#include "acpi_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../firmware/acpi.h"

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
static void acpi_test_invalid_sdt(void);
static void acpi_test_valid_rsdt(void);
static void acpi_test_valid_xsdt(void);
static void acpi_test_invalid_root_tables(void);

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
}

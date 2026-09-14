// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file elf64_test.c
 * @brief ELF64 parser and validation regression tests.
 */

#include "elf64_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../elf/elf64.h"

#include <stddef.h>
#include <stdint.h>

#define ELF64_TEST_IMAGE_SIZE 120U
#define ELF64_TEST_PROGRAM_HEADER_OFFSET 64U

#define ELF64_TEST_MULTI_IMAGE_SIZE 176U
#define ELF64_TEST_PROGRAM_TYPE_NOTE 4U

#define ELF64_TEST_ENTRY_POINT 0x0000000000400000ULL
#define ELF64_TEST_SEGMENT_FILE_OFFSET 0x0000000000001000ULL
#define ELF64_TEST_SEGMENT_VIRTUAL_ADDRESS 0x0000000000400000ULL
#define ELF64_TEST_SEGMENT_FILE_SIZE 0x0000000000000020ULL
#define ELF64_TEST_SEGMENT_MEMORY_SIZE 0x0000000000000040ULL
#define ELF64_TEST_SEGMENT_ALIGNMENT 0x0000000000001000ULL

static void elf64_test_write_u16(
    uint8_t *bytes,
    uint16_t value
);

static void elf64_test_write_u32(
    uint8_t *bytes,
    uint32_t value
);

static void elf64_test_write_u64(
    uint8_t *bytes,
    uint64_t value
);

static void elf64_test_build_valid_image(
    uint8_t image[ELF64_TEST_IMAGE_SIZE]
);

static void elf64_test_valid_image(void);
static void elf64_test_invalid_inputs(void);
static void elf64_test_invalid_identification(void);
static void elf64_test_invalid_header_fields(void);
static void elf64_test_invalid_program_header_table(void);
static void elf64_test_program_header_access(void);
static void elf64_test_multiple_program_headers(void);

static void elf64_test_write_u16(
    uint8_t *bytes,
    uint16_t value)
{
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8);
}

static void elf64_test_write_u32(
    uint8_t *bytes,
    uint32_t value)
{
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8);
    bytes[2] = (uint8_t) (value >> 16);
    bytes[3] = (uint8_t) (value >> 24);
}

static void elf64_test_write_u64(
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

static void elf64_test_build_valid_image(
    uint8_t image[ELF64_TEST_IMAGE_SIZE])
{
    for (size_t index = 0; index < ELF64_TEST_IMAGE_SIZE; ++index) {
        image[index] = 0;
    }

    /*
     * e_ident
     */
    image[0] = ELF64_MAGIC_0;
    image[1] = ELF64_MAGIC_1;
    image[2] = ELF64_MAGIC_2;
    image[3] = ELF64_MAGIC_3;
    image[4] = ELF64_CLASS_64;
    image[5] = ELF64_DATA_LITTLE_ENDIAN;
    image[6] = ELF64_VERSION_CURRENT;

    /*
     * Elf64_Ehdr
     */
    elf64_test_write_u16(
        &image[16],
        ELF64_TYPE_EXECUTABLE
    );

    elf64_test_write_u16(
        &image[18],
        ELF64_MACHINE_X86_64
    );

    elf64_test_write_u32(
        &image[20],
        ELF64_VERSION_CURRENT
    );

    elf64_test_write_u64(
        &image[24],
        ELF64_TEST_ENTRY_POINT
    );

    elf64_test_write_u64(
        &image[32],
        ELF64_TEST_PROGRAM_HEADER_OFFSET
    );

    elf64_test_write_u16(
        &image[52],
        ELF64_HEADER_SIZE
    );

    elf64_test_write_u16(
        &image[54],
        ELF64_PROGRAM_HEADER_SIZE
    );

    elf64_test_write_u16(
        &image[56],
        1
    );

    /*
     * First Elf64_Phdr.
     */
    uint8_t *program_header =
        &image[ELF64_TEST_PROGRAM_HEADER_OFFSET];

    elf64_test_write_u32(
        &program_header[0],
        ELF64_PROGRAM_TYPE_LOAD
    );

    elf64_test_write_u32(
        &program_header[4],
        ELF64_PROGRAM_FLAG_READ |
        ELF64_PROGRAM_FLAG_EXECUTE
    );

    elf64_test_write_u64(
        &program_header[8],
        ELF64_TEST_SEGMENT_FILE_OFFSET
    );

    elf64_test_write_u64(
        &program_header[16],
        ELF64_TEST_SEGMENT_VIRTUAL_ADDRESS
    );

    elf64_test_write_u64(
        &program_header[24],
        ELF64_TEST_SEGMENT_VIRTUAL_ADDRESS
    );

    elf64_test_write_u64(
        &program_header[32],
        ELF64_TEST_SEGMENT_FILE_SIZE
    );

    elf64_test_write_u64(
        &program_header[40],
        ELF64_TEST_SEGMENT_MEMORY_SIZE
    );

    elf64_test_write_u64(
        &program_header[48],
        ELF64_TEST_SEGMENT_ALIGNMENT
    );
}

static void elf64_test_valid_image(void)
{
    uint8_t bytes[ELF64_TEST_IMAGE_SIZE];

    elf64_test_build_valid_image(bytes);

    struct elf64_image image;

    if (!elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "Valid ELF64 image was rejected"
        );
    }

    if (image.data != bytes) {
        kernel_panic(
            "ELF64 image did not retain source buffer"
        );
    }

    if (image.size != sizeof(bytes)) {
        kernel_panic(
            "ELF64 image size is incorrect"
        );
    }

    if (image.entry_point != ELF64_TEST_ENTRY_POINT) {
        kernel_panic(
            "ELF64 entry point is incorrect"
        );
    }

    if (
        image.program_header_offset !=
        ELF64_TEST_PROGRAM_HEADER_OFFSET
    ) {
        kernel_panic(
            "ELF64 program-header offset is incorrect"
        );
    }

    if (image.program_header_count != 1) {
        kernel_panic(
            "ELF64 program-header count is incorrect"
        );
    }

    if (image.load_segment_count != 1) {
        kernel_panic(
            "ELF64 PT_LOAD count is incorrect"
        );
    }

    diagnostics_write(
        "[elf64] Valid image test passed\n"
    );
}

static void elf64_test_invalid_inputs(void)
{
    struct elf64_image image;

    if (elf64_parse(NULL, 0, &image)) {
        kernel_panic(
            "ELF64 parser accepted null input"
        );
    }

    uint8_t bytes[ELF64_HEADER_SIZE] = {0};

    if (elf64_parse(bytes, sizeof(bytes), NULL)) {
        kernel_panic(
            "ELF64 parser accepted null output"
        );
    }

    uint8_t valid[ELF64_TEST_IMAGE_SIZE];
    elf64_test_build_valid_image(valid);

    if (
        elf64_parse(
            valid,
            ELF64_HEADER_SIZE - 1U,
            &image
        )
    ) {
        kernel_panic(
            "ELF64 parser accepted truncated ELF header"
        );
    }

    diagnostics_write(
        "[elf64] Invalid input test passed\n"
    );
}

static void elf64_test_invalid_identification(void)
{
    uint8_t bytes[ELF64_TEST_IMAGE_SIZE];
    struct elf64_image image;

    elf64_test_build_valid_image(bytes);
    bytes[0] = 0;

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted invalid magic"
        );
    }

    elf64_test_build_valid_image(bytes);
    bytes[4] = 1;

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted ELF32 image"
        );
    }

    elf64_test_build_valid_image(bytes);
    bytes[5] = 2;

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted big-endian image"
        );
    }

    elf64_test_build_valid_image(bytes);
    bytes[6] = 0;

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted invalid ident version"
        );
    }

    diagnostics_write(
        "[elf64] Identification validation test passed\n"
    );
}

static void elf64_test_invalid_header_fields(void)
{
    uint8_t bytes[ELF64_TEST_IMAGE_SIZE];
    struct elf64_image image;

    elf64_test_build_valid_image(bytes);
    elf64_test_write_u16(&bytes[16], 1);

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted unsupported file type"
        );
    }

    elf64_test_build_valid_image(bytes);
    elf64_test_write_u16(&bytes[18], 3);

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted unsupported machine"
        );
    }

    elf64_test_build_valid_image(bytes);
    elf64_test_write_u32(&bytes[20], 0);

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted invalid header version"
        );
    }

    elf64_test_build_valid_image(bytes);
    elf64_test_write_u16(
        &bytes[52],
        ELF64_HEADER_SIZE - 1U
    );

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted invalid ELF header size"
        );
    }

    elf64_test_build_valid_image(bytes);
    elf64_test_write_u16(
        &bytes[54],
        ELF64_PROGRAM_HEADER_SIZE - 1U
    );

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted invalid program-header size"
        );
    }

    elf64_test_build_valid_image(bytes);
    elf64_test_write_u16(
        &bytes[56],
        ELF64_PROGRAM_HEADER_COUNT_EXTENDED
    );

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted extended program-header count"
        );
    }

    diagnostics_write(
        "[elf64] Header validation test passed\n"
    );
}

static void elf64_test_invalid_program_header_table(void)
{
    uint8_t bytes[ELF64_TEST_IMAGE_SIZE];
    struct elf64_image image;

    elf64_test_build_valid_image(bytes);

    elf64_test_write_u64(
        &bytes[32],
        ELF64_HEADER_SIZE - 1U
    );

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted overlapping program-header table"
        );
    }

    elf64_test_build_valid_image(bytes);

    elf64_test_write_u64(
        &bytes[32],
        ELF64_TEST_IMAGE_SIZE - 1U
    );

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted truncated program-header table"
        );
    }

    elf64_test_build_valid_image(bytes);

    elf64_test_write_u64(
        &bytes[32],
        UINT64_MAX
    );

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted overflowing program-header offset"
        );
    }

    elf64_test_build_valid_image(bytes);

    elf64_test_write_u16(
        &bytes[56],
        2
    );

    if (elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "ELF64 parser accepted truncated multi-entry program-header table"
        );
    }

    diagnostics_write(
        "[elf64] Program-header table validation test passed\n"
    );
}

static void elf64_test_program_header_access(void)
{
    uint8_t bytes[ELF64_TEST_IMAGE_SIZE];

    elf64_test_build_valid_image(bytes);

    struct elf64_image image;

    if (!elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "Unable to parse valid ELF64 fixture"
        );
    }

    struct elf64_program_header program_header;

    if (
        !elf64_program_header_get(
            &image,
            0,
            &program_header
        )
    ) {
        kernel_panic(
            "Unable to decode valid ELF64 program header"
        );
    }

    if (
        program_header.type !=
        ELF64_PROGRAM_TYPE_LOAD
    ) {
        kernel_panic(
            "ELF64 program-header type is incorrect"
        );
    }

    if (
        program_header.flags !=
        (
            ELF64_PROGRAM_FLAG_READ |
            ELF64_PROGRAM_FLAG_EXECUTE
        )
    ) {
        kernel_panic(
            "ELF64 program-header flags are incorrect"
        );
    }

    if (
        program_header.file_offset !=
        ELF64_TEST_SEGMENT_FILE_OFFSET
    ) {
        kernel_panic(
            "ELF64 program-header file offset is incorrect"
        );
    }

    if (
        program_header.virtual_address !=
        ELF64_TEST_SEGMENT_VIRTUAL_ADDRESS
    ) {
        kernel_panic(
            "ELF64 program-header virtual address is incorrect"
        );
    }

    if (
        program_header.file_size !=
        ELF64_TEST_SEGMENT_FILE_SIZE
    ) {
        kernel_panic(
            "ELF64 program-header file size is incorrect"
        );
    }

    if (
        program_header.memory_size !=
        ELF64_TEST_SEGMENT_MEMORY_SIZE
    ) {
        kernel_panic(
            "ELF64 program-header memory size is incorrect"
        );
    }

    if (
        program_header.alignment !=
        ELF64_TEST_SEGMENT_ALIGNMENT
    ) {
        kernel_panic(
            "ELF64 program-header alignment is incorrect"
        );
    }

    if (
        elf64_program_header_get(
            &image,
            1,
            &program_header
        )
    ) {
        kernel_panic(
            "ELF64 program-header accessor accepted invalid index"
        );
    }

    diagnostics_write(
        "[elf64] Program-header access test passed\n"
    );
}

static void elf64_test_multiple_program_headers(void)
{
    uint8_t bytes[ELF64_TEST_MULTI_IMAGE_SIZE];

    /*
     * Start from the known-valid single-header fixture, then extend the
     * program-header table to contain two entries.
     */
    elf64_test_build_valid_image(bytes);

    elf64_test_write_u16(
        &bytes[56],
        2
    );

    /*
     * Make the first entry a non-loadable segment.
     */
    elf64_test_write_u32(
        &bytes[ELF64_TEST_PROGRAM_HEADER_OFFSET],
        ELF64_TEST_PROGRAM_TYPE_NOTE
    );

    /*
     * Build a second PT_LOAD entry.
     */
    uint8_t *second_program_header =
        &bytes[
            ELF64_TEST_PROGRAM_HEADER_OFFSET +
            ELF64_PROGRAM_HEADER_SIZE
        ];

    elf64_test_write_u32(
        &second_program_header[0],
        ELF64_PROGRAM_TYPE_LOAD
    );

    elf64_test_write_u32(
        &second_program_header[4],
        ELF64_PROGRAM_FLAG_READ |
        ELF64_PROGRAM_FLAG_WRITE
    );

    elf64_test_write_u64(
        &second_program_header[8],
        0x2000ULL
    );

    elf64_test_write_u64(
        &second_program_header[16],
        0x0000000000401000ULL
    );

    elf64_test_write_u64(
        &second_program_header[24],
        0x0000000000401000ULL
    );

    elf64_test_write_u64(
        &second_program_header[32],
        0x10ULL
    );

    elf64_test_write_u64(
        &second_program_header[40],
        0x1000ULL
    );

    elf64_test_write_u64(
        &second_program_header[48],
        0x1000ULL
    );

    struct elf64_image image;

    if (!elf64_parse(bytes, sizeof(bytes), &image)) {
        kernel_panic(
            "Valid multi-header ELF64 image was rejected"
        );
    }

    if (image.program_header_count != 2) {
        kernel_panic(
            "ELF64 multi-header count is incorrect"
        );
    }

    if (image.load_segment_count != 1) {
        kernel_panic(
            "ELF64 PT_LOAD recognition across headers is incorrect"
        );
    }

    struct elf64_program_header program_header;

    if (
        !elf64_program_header_get(
            &image,
            0,
            &program_header
        )
    ) {
        kernel_panic(
            "Unable to decode first ELF64 program header"
        );
    }

    if (
        program_header.type !=
        ELF64_TEST_PROGRAM_TYPE_NOTE
    ) {
        kernel_panic(
            "First ELF64 program-header type is incorrect"
        );
    }

    if (
        !elf64_program_header_get(
            &image,
            1,
            &program_header
        )
    ) {
        kernel_panic(
            "Unable to decode second ELF64 program header"
        );
    }

    if (
        program_header.type !=
        ELF64_PROGRAM_TYPE_LOAD
    ) {
        kernel_panic(
            "Second ELF64 program-header type is incorrect"
        );
    }

    if (
        program_header.virtual_address !=
        0x0000000000401000ULL
    ) {
        kernel_panic(
            "Second ELF64 program-header virtual address is incorrect"
        );
    }

    diagnostics_write(
        "[elf64] Multiple program-header test passed\n"
    );
}

void elf64_test_run(void)
{
    elf64_test_valid_image();
    elf64_test_invalid_inputs();
    elf64_test_invalid_identification();
    elf64_test_invalid_header_fields();
    elf64_test_invalid_program_header_table();
    elf64_test_program_header_access();
    elf64_test_multiple_program_headers();

    diagnostics_write(
        "[elf64] Parser regression tests passed\n"
    );
}

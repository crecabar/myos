// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file elf64_loader_test.c
 * @brief ELF64 PT_LOAD semantic-validation regression tests.
 */

#include "elf64_loader_test.h"

#include "../arch/x86_64/paging.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../elf/elf64.h"
#include "../elf/elf64_loader.h"
#include "../memory/memory.h"
#include "../process/memory.h"

#include <stddef.h>
#include <stdint.h>

#define ELF64_LOADER_TEST_IMAGE_SIZE 0x4000U
#define ELF64_LOADER_TEST_VADDR      0x0000000000400000ULL

#define ELF64_LOADER_TEST_PROGRAM_HEADER_OFFSET ELF64_HEADER_SIZE
#define ELF64_LOADER_TEST_PROGRAM_HEADER_COUNT  2U

#define ELF64_LOADER_TEST_HEADER_TYPE_OFFSET                 16U
#define ELF64_LOADER_TEST_HEADER_MACHINE_OFFSET              18U
#define ELF64_LOADER_TEST_HEADER_VERSION_OFFSET              20U
#define ELF64_LOADER_TEST_HEADER_ENTRY_OFFSET                24U
#define ELF64_LOADER_TEST_HEADER_PROGRAM_HEADER_OFFSET       32U
#define ELF64_LOADER_TEST_HEADER_SIZE_OFFSET                 52U
#define ELF64_LOADER_TEST_HEADER_PROGRAM_HEADER_SIZE_OFFSET  54U
#define ELF64_LOADER_TEST_HEADER_PROGRAM_HEADER_COUNT_OFFSET 56U

#define ELF64_LOADER_TEST_PROGRAM_TYPE_OFFSET            0U
#define ELF64_LOADER_TEST_PROGRAM_FLAGS_OFFSET           4U
#define ELF64_LOADER_TEST_PROGRAM_FILE_OFFSET_OFFSET     8U
#define ELF64_LOADER_TEST_PROGRAM_VIRTUAL_ADDRESS_OFFSET 16U
#define ELF64_LOADER_TEST_PROGRAM_FILE_SIZE_OFFSET       32U
#define ELF64_LOADER_TEST_PROGRAM_MEMORY_SIZE_OFFSET     40U
#define ELF64_LOADER_TEST_PROGRAM_ALIGNMENT_OFFSET       48U

static struct elf64_image elf64_loader_test_image(void);

static struct elf64_program_header elf64_loader_test_program_header(void);

static void elf64_loader_test_write_u16(
    uint8_t *destination,
    uint16_t value
);

static void elf64_loader_test_write_u32(
    uint8_t *destination,
    uint32_t value
);

static void elf64_loader_test_write_u64(
    uint8_t *destination,
    uint64_t value
);

static void elf64_loader_test_build_two_segment_image(
    uint8_t *bytes,
    size_t size,
    bool overlapping
);

static void elf64_loader_test_valid_rx(void);
static void elf64_loader_test_valid_rw(void);
static void elf64_loader_test_filesz_exceeds_memsz(void);
static void elf64_loader_test_file_range(void);
static void elf64_loader_test_file_overflow(void);
static void elf64_loader_test_virtual_range(void);
static void elf64_loader_test_alignment(void);
static void elf64_loader_test_wx(void);
static void elf64_loader_test_zero_memory_size(void);
static void elf64_loader_test_unaligned_segment(void);
static void elf64_loader_test_image_validation(void);
static void elf64_loader_test_entry_point_validation(void);
static void elf64_loader_test_image_mapping(void);
static void elf64_loader_test_image_rollback_existing_mapping(void);
static void elf64_loader_test_partial_segment_rollback(void);
static void elf64_loader_test_unaligned_image_mapping(void);

static struct elf64_image elf64_loader_test_image(void)
{
    struct elf64_image image = {
        .data = NULL,
        .size = ELF64_LOADER_TEST_IMAGE_SIZE,
        .entry_point = ELF64_LOADER_TEST_VADDR,
        .program_header_offset = ELF64_HEADER_SIZE,
        .program_header_count = 1,
        .load_segment_count = 1,
    };

    return image;
}

static struct elf64_program_header elf64_loader_test_program_header(void)
{
    struct elf64_program_header program_header = {
        .type = ELF64_PROGRAM_TYPE_LOAD,
        .flags =
            ELF64_PROGRAM_FLAG_READ |
            ELF64_PROGRAM_FLAG_EXECUTE,
        .file_offset = 0x1000,
        .virtual_address = ELF64_LOADER_TEST_VADDR,
        .physical_address = 0,
        .file_size = 0x800,
        .memory_size = 0x1000,
        .alignment = 0x1000,
    };

    return program_header;
}

static void elf64_loader_test_write_u16(
    uint8_t *destination,
    uint16_t value)
{
    destination[0] =
        (uint8_t) value;

    destination[1] =
        (uint8_t) (value >> 8);
}

static void elf64_loader_test_write_u32(
    uint8_t *destination,
    uint32_t value)
{
    destination[0] =
        (uint8_t) value;

    destination[1] =
        (uint8_t) (value >> 8);

    destination[2] =
        (uint8_t) (value >> 16);

    destination[3] =
        (uint8_t) (value >> 24);
}

static void elf64_loader_test_write_u64(
    uint8_t *destination,
    uint64_t value)
{
    destination[0] =
        (uint8_t) value;

    destination[1] =
        (uint8_t) (value >> 8);

    destination[2] =
        (uint8_t) (value >> 16);

    destination[3] =
        (uint8_t) (value >> 24);

    destination[4] =
        (uint8_t) (value >> 32);

    destination[5] =
        (uint8_t) (value >> 40);

    destination[6] =
        (uint8_t) (value >> 48);

    destination[7] =
        (uint8_t) (value >> 56);
}

static void elf64_loader_test_build_two_segment_image(
    uint8_t *bytes,
    size_t size,
    bool overlapping)
{
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }

    bytes[0] = ELF64_MAGIC_0;
    bytes[1] = ELF64_MAGIC_1;
    bytes[2] = ELF64_MAGIC_2;
    bytes[3] = ELF64_MAGIC_3;

    bytes[4] = ELF64_CLASS_64;
    bytes[5] = ELF64_DATA_LITTLE_ENDIAN;
    bytes[6] = ELF64_VERSION_CURRENT;

    elf64_loader_test_write_u16(
        bytes + ELF64_LOADER_TEST_HEADER_TYPE_OFFSET,
        ELF64_TYPE_EXECUTABLE
    );

    elf64_loader_test_write_u16(
        bytes + ELF64_LOADER_TEST_HEADER_MACHINE_OFFSET,
        ELF64_MACHINE_X86_64
    );

    elf64_loader_test_write_u32(
        bytes + ELF64_LOADER_TEST_HEADER_VERSION_OFFSET,
        ELF64_VERSION_CURRENT
    );

    elf64_loader_test_write_u64(
        bytes + ELF64_LOADER_TEST_HEADER_ENTRY_OFFSET,
        ELF64_LOADER_TEST_VADDR
    );

    elf64_loader_test_write_u64(
        bytes + ELF64_LOADER_TEST_HEADER_PROGRAM_HEADER_OFFSET,
        ELF64_LOADER_TEST_PROGRAM_HEADER_OFFSET
    );

    elf64_loader_test_write_u16(
        bytes + ELF64_LOADER_TEST_HEADER_SIZE_OFFSET,
        ELF64_HEADER_SIZE
    );

    elf64_loader_test_write_u16(
        bytes + ELF64_LOADER_TEST_HEADER_PROGRAM_HEADER_SIZE_OFFSET,
        ELF64_PROGRAM_HEADER_SIZE
    );

    elf64_loader_test_write_u16(
        bytes + ELF64_LOADER_TEST_HEADER_PROGRAM_HEADER_COUNT_OFFSET,
        ELF64_LOADER_TEST_PROGRAM_HEADER_COUNT
    );

    uint8_t *first_program_header =
        bytes +
        ELF64_LOADER_TEST_PROGRAM_HEADER_OFFSET;

    elf64_loader_test_write_u32(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_TYPE_OFFSET,
        ELF64_PROGRAM_TYPE_LOAD
    );

    elf64_loader_test_write_u32(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_FLAGS_OFFSET,
        ELF64_PROGRAM_FLAG_READ |
        ELF64_PROGRAM_FLAG_EXECUTE
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_FILE_OFFSET_OFFSET,
        0x1000
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_VIRTUAL_ADDRESS_OFFSET,
        ELF64_LOADER_TEST_VADDR
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_FILE_SIZE_OFFSET,
        0x400
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_MEMORY_SIZE_OFFSET,
        0x1000
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_ALIGNMENT_OFFSET,
        0x1000
    );

    uint8_t *second_program_header =
        first_program_header +
        ELF64_PROGRAM_HEADER_SIZE;

    elf64_loader_test_write_u32(
        second_program_header +
        ELF64_LOADER_TEST_PROGRAM_TYPE_OFFSET,
        ELF64_PROGRAM_TYPE_LOAD
    );

    elf64_loader_test_write_u32(
        second_program_header +
        ELF64_LOADER_TEST_PROGRAM_FLAGS_OFFSET,
        ELF64_PROGRAM_FLAG_READ |
        ELF64_PROGRAM_FLAG_WRITE
    );

    if (overlapping) {
        elf64_loader_test_write_u64(
            second_program_header +
            ELF64_LOADER_TEST_PROGRAM_FILE_OFFSET_OFFSET,
            0x1800
        );

        elf64_loader_test_write_u64(
            second_program_header +
            ELF64_LOADER_TEST_PROGRAM_VIRTUAL_ADDRESS_OFFSET,
            ELF64_LOADER_TEST_VADDR + 0x800
        );

        elf64_loader_test_write_u64(
            second_program_header +
            ELF64_LOADER_TEST_PROGRAM_MEMORY_SIZE_OFFSET,
            0x800
        );
    } else {
        elf64_loader_test_write_u64(
            second_program_header +
            ELF64_LOADER_TEST_PROGRAM_FILE_OFFSET_OFFSET,
            0x2000
        );

        elf64_loader_test_write_u64(
            second_program_header +
            ELF64_LOADER_TEST_PROGRAM_VIRTUAL_ADDRESS_OFFSET,
            ELF64_LOADER_TEST_VADDR + 0x1000
        );

        elf64_loader_test_write_u64(
            second_program_header +
            ELF64_LOADER_TEST_PROGRAM_MEMORY_SIZE_OFFSET,
            0x1000
        );
    }

    elf64_loader_test_write_u64(
        second_program_header +
        ELF64_LOADER_TEST_PROGRAM_FILE_SIZE_OFFSET,
        0x400
    );

    elf64_loader_test_write_u64(
        second_program_header +
        ELF64_LOADER_TEST_PROGRAM_ALIGNMENT_OFFSET,
        0x1000
    );
}

static void elf64_loader_test_valid_rx(void)
{
    struct elf64_image image =
        elf64_loader_test_image();

    struct elf64_program_header program_header =
        elf64_loader_test_program_header();

    struct elf64_load_segment segment;

    if (
        !elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader rejected valid RX segment"
        );
    }

    if (
        segment.mapping_start !=
        ELF64_LOADER_TEST_VADDR
    ) {
        kernel_panic(
            "ELF64 loader RX mapping start is incorrect"
        );
    }

    if (
        segment.mapping_end !=
        ELF64_LOADER_TEST_VADDR + 0x1000
    ) {
        kernel_panic(
            "ELF64 loader RX mapping end is incorrect"
        );
    }

    if (segment.page_count != 1) {
        kernel_panic(
            "ELF64 loader RX page count is incorrect"
        );
    }

    if (segment.writable) {
        kernel_panic(
            "ELF64 loader RX segment became writable"
        );
    }

    if (!segment.executable) {
        kernel_panic(
            "ELF64 loader RX segment lost execute permission"
        );
    }
}

static void elf64_loader_test_valid_rw(void)
{
    struct elf64_image image =
        elf64_loader_test_image();

    struct elf64_program_header program_header =
        elf64_loader_test_program_header();

    program_header.flags =
        ELF64_PROGRAM_FLAG_READ |
        ELF64_PROGRAM_FLAG_WRITE;

    struct elf64_load_segment segment;

    if (
        !elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader rejected valid RW segment"
        );
    }

    if (!segment.writable) {
        kernel_panic(
            "ELF64 loader RW segment lost write permission"
        );
    }

    if (segment.executable) {
        kernel_panic(
            "ELF64 loader RW segment became executable"
        );
    }
}

static void elf64_loader_test_filesz_exceeds_memsz(void)
{
    struct elf64_image image =
        elf64_loader_test_image();

    struct elf64_program_header program_header =
        elf64_loader_test_program_header();

    program_header.file_size = 0x1001;
    program_header.memory_size = 0x1000;

    struct elf64_load_segment segment;

    if (
        elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader accepted p_filesz greater than p_memsz"
        );
    }
}

static void elf64_loader_test_file_range(void)
{
    struct elf64_image image =
        elf64_loader_test_image();

    struct elf64_program_header program_header =
        elf64_loader_test_program_header();

    program_header.file_offset =
        ELF64_LOADER_TEST_IMAGE_SIZE - 0x100;

    program_header.file_size = 0x200;
    program_header.memory_size = 0x200;

    struct elf64_load_segment segment;

    if (
        elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader accepted file range outside image"
        );
    }
}

static void elf64_loader_test_file_overflow(void)
{
    struct elf64_image image =
        elf64_loader_test_image();

    struct elf64_program_header program_header =
        elf64_loader_test_program_header();

    program_header.file_offset =
        UINT64_MAX - 0x10;

    program_header.file_size = 0x20;
    program_header.memory_size = 0x20;

    struct elf64_load_segment segment;

    if (
        elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader accepted overflowing file range"
        );
    }
}

static void elf64_loader_test_virtual_range(void)
{
    struct elf64_image image =
        elf64_loader_test_image();

    struct elf64_load_segment segment;

    struct elf64_program_header program_header =
        elf64_loader_test_program_header();

    program_header.virtual_address =
        PROCESS_USER_VIRTUAL_MIN - 1;
    program_header.alignment = 1;

    if (
        elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader accepted segment below userspace"
        );
    }

    program_header =
        elf64_loader_test_program_header();

    program_header.virtual_address =
        PROCESS_USER_VIRTUAL_MAX - 0x7FF;
    program_header.alignment = 1;

    program_header.memory_size = 0x1000;

    if (
        elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader accepted segment beyond userspace"
        );
    }

    program_header =
        elf64_loader_test_program_header();

    program_header.virtual_address =
        UINT64_MAX - 0x100;
    program_header.alignment = 1;

    program_header.memory_size = 0x200;

    if (
        elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader accepted overflowing virtual range"
        );
    }
}

static void elf64_loader_test_alignment(void)
{
    struct elf64_image image =
        elf64_loader_test_image();

    struct elf64_load_segment segment;

    struct elf64_program_header program_header =
        elf64_loader_test_program_header();

    program_header.alignment = 24;

    if (
        elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader accepted non-power-of-two alignment"
        );
    }

    program_header =
        elf64_loader_test_program_header();

    program_header.file_offset = 0x1001;

    if (
        elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader accepted incongruent segment alignment"
        );
    }
}

static void elf64_loader_test_wx(void)
{
    struct elf64_image image =
        elf64_loader_test_image();

    struct elf64_program_header program_header =
        elf64_loader_test_program_header();

    program_header.flags =
        ELF64_PROGRAM_FLAG_READ |
        ELF64_PROGRAM_FLAG_WRITE |
        ELF64_PROGRAM_FLAG_EXECUTE;

    struct elf64_load_segment segment;

    if (
        elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader accepted writable executable segment"
        );
    }
}

static void elf64_loader_test_zero_memory_size(void)
{
    struct elf64_image image =
        elf64_loader_test_image();

    struct elf64_program_header program_header =
        elf64_loader_test_program_header();

    program_header.file_size = 0;
    program_header.memory_size = 0;

    struct elf64_load_segment segment;

    if (
        elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader accepted empty load segment"
        );
    }
}

static void elf64_loader_test_unaligned_segment(void)
{
    struct elf64_image image =
        elf64_loader_test_image();

    struct elf64_program_header program_header =
        elf64_loader_test_program_header();

    program_header.file_offset = 0x1123;
    program_header.virtual_address =
        ELF64_LOADER_TEST_VADDR + 0x123;

    program_header.file_size = 0x900;
    program_header.memory_size = 0x1800;
    program_header.alignment = 0x1000;

    struct elf64_load_segment segment;

    if (
        !elf64_load_segment_validate(
            &image,
            &program_header,
            &segment
        )
    ) {
        kernel_panic(
            "ELF64 loader rejected valid unaligned segment"
        );
    }

    if (
        segment.mapping_start !=
        ELF64_LOADER_TEST_VADDR
    ) {
        kernel_panic(
            "ELF64 loader unaligned mapping start is incorrect"
        );
    }

    if (
        segment.mapping_end !=
        ELF64_LOADER_TEST_VADDR + 0x2000
    ) {
        kernel_panic(
            "ELF64 loader unaligned mapping end is incorrect"
        );
    }

    if (segment.page_count != 2) {
        kernel_panic(
            "ELF64 loader unaligned page count is incorrect"
        );
    }
}

static void elf64_loader_test_image_validation(void)
{
    uint8_t bytes[ELF64_LOADER_TEST_IMAGE_SIZE];

    struct elf64_image image;

    elf64_loader_test_build_two_segment_image(
        bytes,
        sizeof(bytes),
        false
    );

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 loader valid image fixture failed to parse"
        );
    }

    if (!elf64_load_image_validate(
        &image
    )) {
        kernel_panic(
            "ELF64 loader rejected valid multi-segment image"
        );
    }

    elf64_loader_test_build_two_segment_image(
        bytes,
        sizeof(bytes),
        true
    );

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 loader overlapping image fixture failed to parse"
        );
    }

    if (elf64_load_image_validate(
        &image
    )) {
        kernel_panic(
            "ELF64 loader accepted overlapping PT_LOAD mappings"
        );
    }
}

static void elf64_loader_test_entry_point_validation(void)
{
    uint8_t bytes[ELF64_LOADER_TEST_IMAGE_SIZE];

    struct elf64_image image;

    /*
     * Entry exactly at the start of the executable PT_LOAD.
     */
    elf64_loader_test_build_two_segment_image(
        bytes,
        sizeof(bytes),
        false
    );

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 entry-point start fixture failed to parse"
        );
    }

    if (!elf64_entry_point_validate(
        &image
    )) {
        kernel_panic(
            "ELF64 loader rejected entry at executable segment start"
        );
    }

    /*
     * Entry inside the executable PT_LOAD.
     */
    elf64_loader_test_write_u64(
        bytes + ELF64_LOADER_TEST_HEADER_ENTRY_OFFSET,
        ELF64_LOADER_TEST_VADDR + 0x200
    );

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 entry-point interior fixture failed to parse"
        );
    }

    if (!elf64_entry_point_validate(
        &image
    )) {
        kernel_panic(
            "ELF64 loader rejected entry inside executable segment"
        );
    }

    /*
     * The executable segment's semantic memory interval ends at +0x1000.
     * The second PT_LOAD begins there, but it is RW rather than executable.
     */
    elf64_loader_test_write_u64(
        bytes + ELF64_LOADER_TEST_HEADER_ENTRY_OFFSET,
        ELF64_LOADER_TEST_VADDR + 0x1000
    );

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 entry-point segment-end fixture failed to parse"
        );
    }

    if (elf64_entry_point_validate(
        &image
    )) {
        kernel_panic(
            "ELF64 loader accepted entry at executable segment end"
        );
    }

    /*
     * Entry strictly inside the second RW PT_LOAD.
     */
    elf64_loader_test_write_u64(
        bytes + ELF64_LOADER_TEST_HEADER_ENTRY_OFFSET,
        ELF64_LOADER_TEST_VADDR + 0x1100
    );

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 entry-point non-executable fixture failed to parse"
        );
    }

    if (elf64_entry_point_validate(
        &image
    )) {
        kernel_panic(
            "ELF64 loader accepted entry inside non-executable segment"
        );
    }

    /*
     * Move the executable segment forward inside its mapped page.
     *
     * Effective page:
     *
     *   0x400000 ----------------------------- 0x401000
     *             0x400123 ======== 0x400623
     *
     * Addresses before 0x400123 and after 0x400623 are mapped as part of
     * the executable page but are not part of the PT_LOAD semantic range.
     */
    elf64_loader_test_build_two_segment_image(
        bytes,
        sizeof(bytes),
        false
    );

    uint8_t *first_program_header =
        bytes +
        ELF64_LOADER_TEST_PROGRAM_HEADER_OFFSET;

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_FILE_OFFSET_OFFSET,
        0x1123
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_VIRTUAL_ADDRESS_OFFSET,
        ELF64_LOADER_TEST_VADDR + 0x123
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_FILE_SIZE_OFFSET,
        0x200
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_MEMORY_SIZE_OFFSET,
        0x500
    );

    /*
     * Entry in leading page-alignment padding.
     */
    elf64_loader_test_write_u64(
        bytes + ELF64_LOADER_TEST_HEADER_ENTRY_OFFSET,
        ELF64_LOADER_TEST_VADDR + 0x100
    );

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 entry-point leading-padding fixture failed to parse"
        );
    }

    if (elf64_entry_point_validate(
        &image
    )) {
        kernel_panic(
            "ELF64 loader accepted entry in leading page padding"
        );
    }

    /*
     * Entry in trailing page-alignment padding.
     *
     * Semantic segment end is 0x400623.
     */
    elf64_loader_test_write_u64(
        bytes + ELF64_LOADER_TEST_HEADER_ENTRY_OFFSET,
        ELF64_LOADER_TEST_VADDR + 0x700
    );

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 entry-point trailing-padding fixture failed to parse"
        );
    }

    if (elf64_entry_point_validate(
        &image
    )) {
        kernel_panic(
            "ELF64 loader accepted entry in trailing page padding"
        );
    }

    /*
     * Entry outside every PT_LOAD segment.
     */
    elf64_loader_test_write_u64(
        bytes + ELF64_LOADER_TEST_HEADER_ENTRY_OFFSET,
        ELF64_LOADER_TEST_VADDR + 0x3000
    );

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 entry-point outside-image fixture failed to parse"
        );
    }

    if (elf64_entry_point_validate(
        &image
    )) {
        kernel_panic(
            "ELF64 loader accepted entry outside all PT_LOAD segments"
        );
    }

    /*
     * A valid-looking entry point must not make an otherwise invalid image
     * executable. Build overlapping PT_LOAD mappings while leaving e_entry
     * at the beginning of the first RX segment.
     */
    elf64_loader_test_build_two_segment_image(
        bytes,
        sizeof(bytes),
        true
    );

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 entry-point invalid-image fixture failed to parse"
        );
    }

    if (elf64_entry_point_validate(
        &image
    )) {
        kernel_panic(
            "ELF64 loader accepted entry from invalid load image"
        );
    }

    diagnostics_write(
        "[elf64] Entry-point validation test passed\n"
    );
}

static void elf64_loader_test_image_mapping(void)
{
    uint8_t bytes[ELF64_LOADER_TEST_IMAGE_SIZE];

    elf64_loader_test_build_two_segment_image(
        bytes,
        sizeof(bytes),
        false
    );

    for (size_t index = 0; index < 0x400; ++index) {
        bytes[0x1000 + index] =
            (uint8_t) (0x40U + (index & 0x3fU));

        bytes[0x2000 + index] =
            (uint8_t) (0x80U + (index & 0x3fU));
    }

    struct elf64_image image;

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 loader mapping fixture failed to parse"
        );
    }

    uint64_t free_before =
        physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create ELF64 loader test address space"
        );
    }

    struct elf64_loaded_image loaded_image = {0};

    if (!elf64_load_image(
        &image,
        &memory,
        &loaded_image
    )) {
        kernel_panic(
            "ELF64 loader failed to map valid image"
        );
    }

    uint8_t code_bytes[0x500];
    uint8_t data_bytes[0x500];

    if (!process_memory_read(
        &memory,
        ELF64_LOADER_TEST_VADDR,
        code_bytes,
        sizeof(code_bytes)
    )) {
        kernel_panic(
            "Unable to read ELF64 loader code segment"
        );
    }

    if (!process_memory_read(
        &memory,
        ELF64_LOADER_TEST_VADDR + 0x1000,
        data_bytes,
        sizeof(data_bytes)
    )) {
        kernel_panic(
            "Unable to read ELF64 loader data segment"
        );
    }

    for (size_t index = 0; index < 0x400; ++index) {
        uint8_t expected_code =
            (uint8_t) (0x40U + (index & 0x3fU));

        uint8_t expected_data =
            (uint8_t) (0x80U + (index & 0x3fU));

        if (code_bytes[index] != expected_code) {
            kernel_panic(
                "ELF64 loader code bytes do not match file image"
            );
        }

        if (data_bytes[index] != expected_data) {
            kernel_panic(
                "ELF64 loader data bytes do not match file image"
            );
        }
    }

    for (
        size_t index = 0x400;
        index < 0x500;
        ++index
    ) {
        if (code_bytes[index] != 0) {
            kernel_panic(
                "ELF64 loader code BSS is not zero-filled"
            );
        }

        if (data_bytes[index] != 0) {
            kernel_panic(
                "ELF64 loader data BSS is not zero-filled"
            );
        }
    }

    struct paging_translation code_translation;

    if (!paging_translate_address_space(
        &memory.address_space,
        ELF64_LOADER_TEST_VADDR,
        &code_translation
    )) {
        kernel_panic(
            "ELF64 loader code segment is not mapped"
        );
    }

    if (
        code_translation.page_size !=
        PAGING_PAGE_SIZE_4K
    ) {
        kernel_panic(
            "ELF64 loader code segment is not a 4 KiB page"
        );
    }

    if (
        (
            code_translation.pt_entry &
            PAGE_ENTRY_USER
        ) == 0
    ) {
        kernel_panic(
            "ELF64 loader code segment is not user-accessible"
        );
    }

    if (
        (
            code_translation.pt_entry &
            PAGE_ENTRY_WRITABLE
        ) != 0
    ) {
        kernel_panic(
            "ELF64 loader code segment became writable"
        );
    }

    if (
        (
            code_translation.pt_entry &
            PAGE_ENTRY_NO_EXECUTE
        ) != 0
    ) {
        kernel_panic(
            "ELF64 loader code segment became non-executable"
        );
    }

    struct paging_translation data_translation;

    if (!paging_translate_address_space(
        &memory.address_space,
        ELF64_LOADER_TEST_VADDR + 0x1000,
        &data_translation
    )) {
        kernel_panic(
            "ELF64 loader data segment is not mapped"
        );
    }

    if (
        data_translation.page_size !=
        PAGING_PAGE_SIZE_4K
    ) {
        kernel_panic(
            "ELF64 loader data segment is not a 4 KiB page"
        );
    }

    if (
        (
            data_translation.pt_entry &
            PAGE_ENTRY_USER
        ) == 0
    ) {
        kernel_panic(
            "ELF64 loader data segment is not user-accessible"
        );
    }

    if (
        (
            data_translation.pt_entry &
            PAGE_ENTRY_WRITABLE
        ) == 0
    ) {
        kernel_panic(
            "ELF64 loader data segment is not writable"
        );
    }

    if (
        (
            data_translation.pt_entry &
            PAGE_ENTRY_NO_EXECUTE
        ) == 0
    ) {
        kernel_panic(
            "ELF64 loader data segment became executable"
        );
    }

    if (
        loaded_image.segment_count !=
        image.load_segment_count
    ) {
        kernel_panic(
            "ELF64 loaded image segment count is incorrect"
        );
    }

    if (
        loaded_image.segments == NULL
    ) {
        kernel_panic(
            "ELF64 loaded image metadata is missing"
        );
    }

    if (
        loaded_image.segments[0].mapping_start !=
        ELF64_LOADER_TEST_VADDR ||
        loaded_image.segments[1].mapping_start !=
        ELF64_LOADER_TEST_VADDR + 0x1000
    ) {
        kernel_panic(
            "ELF64 loaded image segment metadata is incorrect"
        );
    }

    if (!elf64_unload_image(
        &memory,
        &loaded_image
    )) {
        kernel_panic(
            "Unable to unload ELF64 loader test image"
        );
    }

    if (
        loaded_image.segments != NULL ||
        loaded_image.segment_count != 0
    ) {
        kernel_panic(
            "ELF64 unloaded image descriptor was not cleared"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy ELF64 loader test address space"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "ELF64 loader mapping test leaked physical frames"
        );
    }

    diagnostics_write(
        "[elf64] Load-image mapping test passed\n"
    );
}

static void elf64_loader_test_image_rollback_existing_mapping(void)
{
    uint8_t bytes[ELF64_LOADER_TEST_IMAGE_SIZE];

    elf64_loader_test_build_two_segment_image(
        bytes,
        sizeof(bytes),
        false
    );

    struct elf64_image image;

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 loader rollback fixture failed to parse"
        );
    }

    uint64_t free_before =
        physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create ELF64 rollback test address space"
        );
    }

    uint64_t conflicting_address =
        ELF64_LOADER_TEST_VADDR + 0x1000;

    if (!process_memory_allocate_page(
        &memory,
        conflicting_address,
        true
    )) {
        kernel_panic(
            "Unable to create ELF64 rollback conflict mapping"
        );
    }

    struct paging_translation conflict_before;

    if (!paging_translate_address_space(
        &memory.address_space,
        conflicting_address,
        &conflict_before
    )) {
        kernel_panic(
            "ELF64 rollback conflict mapping is missing"
        );
    }

    uint8_t sentinel = 0xa5U;

    if (!process_memory_write(
        &memory,
        conflicting_address,
        &sentinel,
        sizeof(sentinel)
    )) {
        kernel_panic(
            "Unable to initialize ELF64 rollback conflict mapping"
        );
    }

    struct elf64_loaded_image loaded_image = {0};

    if (elf64_load_image(
        &image,
        &memory,
        &loaded_image
    )) {
        kernel_panic(
            "ELF64 loader accepted conflicting process mapping"
        );
    }

    if (
        loaded_image.segments != NULL ||
        loaded_image.segment_count != 0
    ) {
        kernel_panic(
            "ELF64 failed load retained ownership metadata"
        );
    }

    struct paging_translation rolled_back_translation;

    if (paging_translate_address_space(
        &memory.address_space,
        ELF64_LOADER_TEST_VADDR,
        &rolled_back_translation
    )) {
        kernel_panic(
            "ELF64 loader left first segment mapped after rollback"
        );
    }

    struct paging_translation conflict_after;

    if (!paging_translate_address_space(
        &memory.address_space,
        conflicting_address,
        &conflict_after
    )) {
        kernel_panic(
            "ELF64 loader removed pre-existing conflict mapping"
        );
    }

    if (
        conflict_after.physical_address !=
        conflict_before.physical_address
    ) {
        kernel_panic(
            "ELF64 loader replaced pre-existing conflict mapping"
        );
    }

    uint8_t observed_sentinel = 0;

    if (!process_memory_read(
        &memory,
        conflicting_address,
        &observed_sentinel,
        sizeof(observed_sentinel)
    )) {
        kernel_panic(
            "Unable to read ELF64 rollback conflict mapping"
        );
    }

    if (observed_sentinel != sentinel) {
        kernel_panic(
            "ELF64 loader modified pre-existing conflict mapping"
        );
    }

    if (!process_memory_release_page(
        &memory,
        conflicting_address
    )) {
        kernel_panic(
            "Unable to release ELF64 rollback conflict mapping"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy ELF64 rollback test address space"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "ELF64 loader rollback test leaked physical frames"
        );
    }

    diagnostics_write(
        "[elf64] Load-image rollback test passed\n"
    );
}

static void elf64_loader_test_partial_segment_rollback(void)
{
    uint8_t bytes[ELF64_LOADER_TEST_IMAGE_SIZE];

    elf64_loader_test_build_two_segment_image(
        bytes,
        sizeof(bytes),
        false
    );

    uint8_t *second_program_header =
        bytes +
        ELF64_LOADER_TEST_PROGRAM_HEADER_OFFSET +
        ELF64_PROGRAM_HEADER_SIZE;

    elf64_loader_test_write_u64(
        second_program_header +
        ELF64_LOADER_TEST_PROGRAM_MEMORY_SIZE_OFFSET,
        0x2000
    );

    struct elf64_image image;

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 loader partial rollback fixture failed to parse"
        );
    }

    uint64_t free_before =
        physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create ELF64 partial rollback address space"
        );
    }

    uint64_t conflicting_address =
        ELF64_LOADER_TEST_VADDR + 0x2000;

    if (!process_memory_allocate_page(
        &memory,
        conflicting_address,
        true
    )) {
        kernel_panic(
            "Unable to create ELF64 partial rollback conflict mapping"
        );
    }

    struct paging_translation conflict_before;

    if (!paging_translate_address_space(
        &memory.address_space,
        conflicting_address,
        &conflict_before
    )) {
        kernel_panic(
            "ELF64 partial rollback conflict mapping is missing"
        );
    }

    uint8_t sentinel = 0x5aU;

    if (!process_memory_write(
        &memory,
        conflicting_address,
        &sentinel,
        sizeof(sentinel)
    )) {
        kernel_panic(
            "Unable to initialize ELF64 partial rollback conflict"
        );
    }

    struct elf64_loaded_image loaded_image = {0};

    if (elf64_load_image(
        &image,
        &memory,
        &loaded_image
    )) {
        kernel_panic(
            "ELF64 loader accepted partially conflicting segment"
        );
    }

    if (
        loaded_image.segments != NULL ||
        loaded_image.segment_count != 0
    ) {
        kernel_panic(
            "ELF64 partial rollback retained ownership metadata"
        );
    }

    struct paging_translation translation;

    if (paging_translate_address_space(
        &memory.address_space,
        ELF64_LOADER_TEST_VADDR,
        &translation
    )) {
        kernel_panic(
            "ELF64 loader left first segment after partial rollback"
        );
    }

    if (paging_translate_address_space(
        &memory.address_space,
        ELF64_LOADER_TEST_VADDR + 0x1000,
        &translation
    )) {
        kernel_panic(
            "ELF64 loader left partial second segment mapped"
        );
    }

    struct paging_translation conflict_after;

    if (!paging_translate_address_space(
        &memory.address_space,
        conflicting_address,
        &conflict_after
    )) {
        kernel_panic(
            "ELF64 loader removed partial rollback conflict mapping"
        );
    }

    if (
        conflict_after.physical_address !=
        conflict_before.physical_address
    ) {
        kernel_panic(
            "ELF64 loader replaced partial rollback conflict mapping"
        );
    }

    uint8_t observed_sentinel = 0;

    if (!process_memory_read(
        &memory,
        conflicting_address,
        &observed_sentinel,
        sizeof(observed_sentinel)
    )) {
        kernel_panic(
            "Unable to read ELF64 partial rollback conflict mapping"
        );
    }

    if (observed_sentinel != sentinel) {
        kernel_panic(
            "ELF64 loader modified partial rollback conflict mapping"
        );
    }

    if (!process_memory_release_page(
        &memory,
        conflicting_address
    )) {
        kernel_panic(
            "Unable to release ELF64 partial rollback conflict mapping"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy ELF64 partial rollback address space"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "ELF64 partial rollback test leaked physical frames"
        );
    }

    diagnostics_write(
        "[elf64] Partial-segment rollback test passed\n"
    );
}

static void elf64_loader_test_unaligned_image_mapping(void)
{
    uint8_t bytes[ELF64_LOADER_TEST_IMAGE_SIZE];

    elf64_loader_test_build_two_segment_image(
        bytes,
        sizeof(bytes),
        false
    );

    uint8_t *first_program_header =
        bytes +
        ELF64_LOADER_TEST_PROGRAM_HEADER_OFFSET;

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_FILE_OFFSET_OFFSET,
        0x1123
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_VIRTUAL_ADDRESS_OFFSET,
        ELF64_LOADER_TEST_VADDR + 0x123
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_FILE_SIZE_OFFSET,
        0x200
    );

    elf64_loader_test_write_u64(
        first_program_header +
        ELF64_LOADER_TEST_PROGRAM_MEMORY_SIZE_OFFSET,
        0x500
    );

    for (size_t index = 0; index < 0x200; ++index) {
        bytes[0x1123 + index] =
            (uint8_t) (0x20U + (index & 0x5fU));
    }

    struct elf64_image image;

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 loader unaligned fixture failed to parse"
        );
    }

    uint64_t free_before =
        physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create ELF64 unaligned test address space"
        );
    }

    struct elf64_loaded_image loaded_image = {0};

    if (!elf64_load_image(
        &image,
        &memory,
        &loaded_image
    )) {
        kernel_panic(
            "ELF64 loader rejected valid unaligned image"
        );
    }

    uint8_t page[ELF64_LOADER_PAGE_SIZE];

    if (!process_memory_read(
        &memory,
        ELF64_LOADER_TEST_VADDR,
        page,
        sizeof(page)
    )) {
        kernel_panic(
            "Unable to read ELF64 unaligned loaded page"
        );
    }

    for (size_t index = 0; index < 0x123; ++index) {
        if (page[index] != 0) {
            kernel_panic(
                "ELF64 loader left non-zero leading page padding"
            );
        }
    }

    for (size_t index = 0; index < 0x200; ++index) {
        uint8_t expected =
            (uint8_t) (0x20U + (index & 0x5fU));

        if (page[0x123 + index] != expected) {
            kernel_panic(
                "ELF64 loader unaligned file bytes are incorrect"
            );
        }
    }

    for (
        size_t index = 0x323;
        index < 0x623;
        ++index
    ) {
        if (page[index] != 0) {
            kernel_panic(
                "ELF64 loader unaligned BSS is not zero-filled"
            );
        }
    }

    for (
        size_t index = 0x623;
        index < ELF64_LOADER_PAGE_SIZE;
        ++index
    ) {
        if (page[index] != 0) {
            kernel_panic(
                "ELF64 loader left non-zero trailing page padding"
            );
        }
    }

    if (
        loaded_image.segment_count !=
        image.load_segment_count
    ) {
        kernel_panic(
            "ELF64 unaligned loaded segment count is incorrect"
        );
    }

    if (
        loaded_image.segments == NULL
    ) {
        kernel_panic(
            "ELF64 unaligned loaded image metadata is missing"
        );
    }

    if (!elf64_unload_image(
        &memory,
        &loaded_image
    )) {
        kernel_panic(
            "Unable to unload ELF64 unaligned image"
        );
    }

    if (
        loaded_image.segments != NULL ||
        loaded_image.segment_count != 0
    ) {
        kernel_panic(
            "ELF64 unaligned image descriptor was not cleared"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy ELF64 unaligned test address space"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "ELF64 unaligned image test leaked physical frames"
        );
    }

    diagnostics_write(
        "[elf64] Unaligned load-image mapping test passed\n"
    );
}

static void elf64_loader_test_reject_owned_descriptor(void)
{
    uint8_t bytes[ELF64_LOADER_TEST_IMAGE_SIZE];

    elf64_loader_test_build_two_segment_image(
        bytes,
        sizeof(bytes),
        false
    );

    struct elf64_image image;

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF64 owned-descriptor fixture failed to parse"
        );
    }

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create ELF64 owned-descriptor address space"
        );
    }

    struct elf64_load_segment sentinel_segment;

    struct elf64_loaded_image loaded_image = {
        .segments = &sentinel_segment,
        .segment_count = 1,
    };

    if (elf64_load_image(
        &image,
        &memory,
        &loaded_image
    )) {
        kernel_panic(
            "ELF64 loader accepted non-empty output descriptor"
        );
    }

    if (
        loaded_image.segments !=
        &sentinel_segment ||
        loaded_image.segment_count != 1
    ) {
        kernel_panic(
            "ELF64 loader modified rejected output descriptor"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Unable to destroy ELF64 owned-descriptor address space"
        );
    }
}

void elf64_loader_test_run(void)
{
    elf64_loader_test_valid_rx();
    elf64_loader_test_valid_rw();
    elf64_loader_test_filesz_exceeds_memsz();
    elf64_loader_test_file_range();
    elf64_loader_test_file_overflow();
    elf64_loader_test_virtual_range();
    elf64_loader_test_alignment();
    elf64_loader_test_wx();
    elf64_loader_test_zero_memory_size();
    elf64_loader_test_unaligned_segment();
    elf64_loader_test_image_validation();
    elf64_loader_test_entry_point_validation();
    elf64_loader_test_reject_owned_descriptor();
    elf64_loader_test_image_mapping();
    elf64_loader_test_image_rollback_existing_mapping();
    elf64_loader_test_partial_segment_rollback();
    elf64_loader_test_unaligned_image_mapping();

    diagnostics_write(
        "[elf64] Loader regression tests passed\n"
    );
}

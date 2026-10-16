// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file elf64.c
 * @brief Pure ELF64 executable parsing and validation.
 */

#include "elf64.h"

#include <stddef.h>
#include <stdint.h>

/*
 * ELF e_ident offsets.
 */
#define ELF64_IDENT_MAGIC_0_OFFSET 0U
#define ELF64_IDENT_MAGIC_1_OFFSET 1U
#define ELF64_IDENT_MAGIC_2_OFFSET 2U
#define ELF64_IDENT_MAGIC_3_OFFSET 3U
#define ELF64_IDENT_CLASS_OFFSET   4U
#define ELF64_IDENT_DATA_OFFSET    5U
#define ELF64_IDENT_VERSION_OFFSET 6U

/*
 * ELF64 executable-header field offsets.
 */
#define ELF64_HEADER_TYPE_OFFSET                 16U
#define ELF64_HEADER_MACHINE_OFFSET              18U
#define ELF64_HEADER_VERSION_OFFSET              20U
#define ELF64_HEADER_ENTRY_OFFSET                24U
#define ELF64_HEADER_PROGRAM_HEADER_OFFSET       32U
#define ELF64_HEADER_HEADER_SIZE_OFFSET          52U
#define ELF64_HEADER_PROGRAM_HEADER_SIZE_OFFSET  54U
#define ELF64_HEADER_PROGRAM_HEADER_COUNT_OFFSET 56U

/*
 * ELF64 program-header field offsets.
 */
#define ELF64_PROGRAM_TYPE_OFFSET             0U
#define ELF64_PROGRAM_FLAGS_OFFSET            4U
#define ELF64_PROGRAM_FILE_OFFSET_OFFSET      8U
#define ELF64_PROGRAM_VIRTUAL_ADDRESS_OFFSET  16U
#define ELF64_PROGRAM_PHYSICAL_ADDRESS_OFFSET 24U
#define ELF64_PROGRAM_FILE_SIZE_OFFSET        32U
#define ELF64_PROGRAM_MEMORY_SIZE_OFFSET      40U
#define ELF64_PROGRAM_ALIGNMENT_OFFSET        48U

static uint16_t elf64_read_u16(const uint8_t *bytes);
static uint32_t elf64_read_u32(const uint8_t *bytes);
static uint64_t elf64_read_u64(const uint8_t *bytes);

static bool elf64_range_valid(
    size_t image_size,
    uint64_t offset,
    uint64_t length
);

static bool elf64_ident_valid(const uint8_t *data);

static void elf64_image_clear(
    struct elf64_image *image
);

static void elf64_program_header_clear(
    struct elf64_program_header *program_header
);

static bool elf64_program_header_decode(
    const struct elf64_source *source,
    uint64_t offset,
    struct elf64_program_header *program_header
);

static bool elf64_memory_source_read(
    const void *context,
    uint64_t offset,
    void *buffer,
    size_t size
);

static bool elf64_source_read_exact(
    const struct elf64_source *source,
    uint64_t offset,
    void *buffer,
    size_t size
);

static uint16_t elf64_read_u16(const uint8_t *bytes)
{
    return
        (uint16_t) bytes[0] |
        ((uint16_t) bytes[1] << 8);
}

static uint32_t elf64_read_u32(const uint8_t *bytes)
{
    return
        (uint32_t) bytes[0] |
        ((uint32_t) bytes[1] << 8) |
        ((uint32_t) bytes[2] << 16) |
        ((uint32_t) bytes[3] << 24);
}

static uint64_t elf64_read_u64(const uint8_t *bytes)
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

static bool elf64_range_valid(
    size_t image_size,
    uint64_t offset,
    uint64_t length)
{
    uint64_t available_size =
        (uint64_t) image_size;

    if (offset > available_size) {
        return false;
    }

    return length <= available_size - offset;
}

static bool elf64_ident_valid(const uint8_t *data)
{
    if (
        data[ELF64_IDENT_MAGIC_0_OFFSET] !=
        ELF64_MAGIC_0
    ) {
        return false;
    }

    if (
        data[ELF64_IDENT_MAGIC_1_OFFSET] !=
        ELF64_MAGIC_1
    ) {
        return false;
    }

    if (
        data[ELF64_IDENT_MAGIC_2_OFFSET] !=
        ELF64_MAGIC_2
    ) {
        return false;
    }

    if (
        data[ELF64_IDENT_MAGIC_3_OFFSET] !=
        ELF64_MAGIC_3
    ) {
        return false;
    }

    if (
        data[ELF64_IDENT_CLASS_OFFSET] !=
        ELF64_CLASS_64
    ) {
        return false;
    }

    if (
        data[ELF64_IDENT_DATA_OFFSET] !=
        ELF64_DATA_LITTLE_ENDIAN
    ) {
        return false;
    }

    if (
        data[ELF64_IDENT_VERSION_OFFSET] !=
        ELF64_VERSION_CURRENT
    ) {
        return false;
    }

    return true;
}

static void elf64_image_clear(
    struct elf64_image *image)
{
    image->source_context = NULL;
    image->source_read = NULL;
    image->size = 0;
    image->entry_point = 0;
    image->program_header_offset = 0;
    image->program_header_count = 0;
    image->load_segment_count = 0;
}

static void elf64_program_header_clear(
    struct elf64_program_header *program_header)
{
    program_header->type = 0;
    program_header->flags = 0;
    program_header->file_offset = 0;
    program_header->virtual_address = 0;
    program_header->physical_address = 0;
    program_header->file_size = 0;
    program_header->memory_size = 0;
    program_header->alignment = 0;
}

static bool elf64_program_header_decode(
    const struct elf64_source *source,
    uint64_t offset,
    struct elf64_program_header *program_header)
{
    uint8_t bytes[ELF64_PROGRAM_HEADER_SIZE];

    if (!elf64_source_read_exact(
        source,
        offset,
        bytes,
        sizeof(bytes)
    )) {
        return false;
    }

    program_header->type =
        elf64_read_u32(
            bytes +
            ELF64_PROGRAM_TYPE_OFFSET
        );

    program_header->flags =
        elf64_read_u32(
            bytes +
            ELF64_PROGRAM_FLAGS_OFFSET
        );

    program_header->file_offset =
        elf64_read_u64(
            bytes +
            ELF64_PROGRAM_FILE_OFFSET_OFFSET
        );

    program_header->virtual_address =
        elf64_read_u64(
            bytes +
            ELF64_PROGRAM_VIRTUAL_ADDRESS_OFFSET
        );

    program_header->physical_address =
        elf64_read_u64(
            bytes +
            ELF64_PROGRAM_PHYSICAL_ADDRESS_OFFSET
        );

    program_header->file_size =
        elf64_read_u64(
            bytes +
            ELF64_PROGRAM_FILE_SIZE_OFFSET
        );

    program_header->memory_size =
        elf64_read_u64(
            bytes +
            ELF64_PROGRAM_MEMORY_SIZE_OFFSET
        );

    program_header->alignment =
        elf64_read_u64(
            bytes +
            ELF64_PROGRAM_ALIGNMENT_OFFSET
        );

    return true;
}

static bool elf64_source_read_exact(
    const struct elf64_source *source,
    uint64_t offset,
    void *buffer,
    size_t size)
{
    if (
        source == NULL ||
        source->read == NULL
    ) {
        return false;
    }

    if (!elf64_range_valid(
        source->size,
        offset,
        (uint64_t) size
    )) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    if (buffer == NULL) {
        return false;
    }

    return source->read(
        source->context,
        offset,
        buffer,
        size
    );
}

static bool elf64_memory_source_read(
    const void *context,
    uint64_t offset,
    void *buffer,
    size_t size)
{
    if (
        context == NULL ||
        buffer == NULL
    ) {
        return false;
    }

    const uint8_t *source =
        (const uint8_t *) context;

    uint8_t *destination =
        (uint8_t *) buffer;

    for (size_t index = 0; index < size; ++index) {
        destination[index] =
            source[(size_t) offset + index];
    }

    return true;
}

bool elf64_parse(
    const void *data,
    size_t size,
    struct elf64_image *image)
{
    if (image == NULL) {
        return false;
    }

    if (data == NULL) {
        elf64_image_clear(image);

        return false;
    }

    struct elf64_source source = {
        .context = data,
        .size = size,
        .read = elf64_memory_source_read,
    };

    return elf64_parse_source(
        &source,
        image
    );
}

bool elf64_parse_source(
    const struct elf64_source *source,
    struct elf64_image *image)
{
    if (image == NULL) {
        return false;
    }

    elf64_image_clear(
        image
    );

    if (
        source == NULL ||
        source->read == NULL
    ) {
        return false;
    }

    uint8_t header[
        ELF64_HEADER_SIZE
    ];

    if (!elf64_source_read_exact(
        source,
        0,
        header,
        sizeof(header)
    )) {
        return false;
    }

    if (!elf64_ident_valid(
        header
    )) {
        return false;
    }

    uint16_t type =
        elf64_read_u16(
            header +
            ELF64_HEADER_TYPE_OFFSET
        );

    if (type != ELF64_TYPE_EXECUTABLE) {
        return false;
    }

    uint16_t machine =
        elf64_read_u16(
            header +
            ELF64_HEADER_MACHINE_OFFSET
        );

    if (machine != ELF64_MACHINE_X86_64) {
        return false;
    }

    uint32_t version =
        elf64_read_u32(
            header +
            ELF64_HEADER_VERSION_OFFSET
        );

    if (version != ELF64_VERSION_CURRENT) {
        return false;
    }

    uint16_t header_size =
        elf64_read_u16(
            header +
            ELF64_HEADER_HEADER_SIZE_OFFSET
        );

    if (header_size != ELF64_HEADER_SIZE) {
        return false;
    }

    uint16_t program_header_size =
        elf64_read_u16(
            header +
            ELF64_HEADER_PROGRAM_HEADER_SIZE_OFFSET
        );

    if (
        program_header_size !=
        ELF64_PROGRAM_HEADER_SIZE
    ) {
        return false;
    }

    uint16_t program_header_count =
        elf64_read_u16(
            header +
            ELF64_HEADER_PROGRAM_HEADER_COUNT_OFFSET
        );

    if (
        program_header_count ==
        ELF64_PROGRAM_HEADER_COUNT_EXTENDED
    ) {
        return false;
    }

    uint64_t program_header_offset =
        elf64_read_u64(
            header +
            ELF64_HEADER_PROGRAM_HEADER_OFFSET
        );

    if (
        program_header_count > 0 &&
        program_header_offset <
            ELF64_HEADER_SIZE
    ) {
        return false;
    }

    uint64_t program_header_table_size =
        (uint64_t) program_header_count *
        ELF64_PROGRAM_HEADER_SIZE;

    if (
        !elf64_range_valid(
            source->size,
            program_header_offset,
            program_header_table_size
        )
    ) {
        return false;
    }

    uint16_t load_segment_count =
        0;

    for (
        uint16_t index = 0;
        index < program_header_count;
        ++index
    ) {
        uint64_t header_offset =
            program_header_offset +
            (uint64_t) index *
            ELF64_PROGRAM_HEADER_SIZE;

        struct elf64_program_header program_header;

        if (!elf64_program_header_decode(
            source,
            header_offset,
            &program_header
        )) {
            return false;
        }

        if (
            program_header.type ==
            ELF64_PROGRAM_TYPE_LOAD
        ) {
            ++load_segment_count;
        }
    }

    image->source_context =
        source->context;

    image->source_read =
        source->read;

    image->size =
        source->size;

    image->entry_point =
        elf64_read_u64(
            header +
            ELF64_HEADER_ENTRY_OFFSET
        );

    image->program_header_offset =
        program_header_offset;

    image->program_header_count =
        program_header_count;

    image->load_segment_count =
        load_segment_count;

    return true;
}

bool elf64_program_header_get(
    const struct elf64_image *image,
    size_t index,
    struct elf64_program_header *program_header)
{
    if (program_header == NULL) {
        return false;
    }

    elf64_program_header_clear(
        program_header
    );

    if (
        image == NULL ||
        image->source_read == NULL
    ) {
        return false;
    }

    if (
        index >=
        image->program_header_count
    ) {
        return false;
    }

    uint64_t header_offset =
        image->program_header_offset +
        (uint64_t) index *
        ELF64_PROGRAM_HEADER_SIZE;

    struct elf64_source source = {
        .context =
            image->source_context,
        .size =
            image->size,
        .read =
            image->source_read,
    };

    return elf64_program_header_decode(
        &source,
        header_offset,
        program_header
    );
}

bool elf64_image_read(
    const struct elf64_image *image,
    uint64_t offset,
    void *buffer,
    size_t size)
{
    if (image == NULL) {
        return false;
    }

    struct elf64_source source = {
        .context =
            image->source_context,
        .size =
            image->size,
        .read =
            image->source_read,
    };

    return elf64_source_read_exact(
        &source,
        offset,
        buffer,
        size
    );
}

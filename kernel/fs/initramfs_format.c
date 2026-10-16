// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file initramfs_format.c
 * @brief Bounded parser for the initramfs CPIO newc format.
 */

#include "initramfs_format.h"

#include "../vfs/path.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CPIO newc constants
#define INITRAMFS_NEWC_MAGIC_SIZE 6U
#define INITRAMFS_NEWC_HEADER_SIZE 110U

#define INITRAMFS_NEWC_FIELD_COUNT 13U
#define INITRAMFS_NEWC_FIELD_SIZE 8U

#define INITRAMFS_NEWC_FIELD_MODE 1U
#define INITRAMFS_NEWC_FIELD_FILE_SIZE 6U
#define INITRAMFS_NEWC_FIELD_NAME_SIZE 11U
#define INITRAMFS_NEWC_FIELD_CHECK 12U

#define INITRAMFS_NEWC_ALIGNMENT 4U

#define INITRAMFS_NEWC_MODE_TYPE_MASK 0170000U
#define INITRAMFS_NEWC_MODE_DIRECTORY 0040000U
#define INITRAMFS_NEWC_MODE_REGULAR 0100000U

#define INITRAMFS_NEWC_TRAILER_LENGTH 10U

static const uint8_t initramfs_newc_magic[
    INITRAMFS_NEWC_MAGIC_SIZE
] = {
    '0', '7', '0', '7', '0', '1',
};

static const char initramfs_newc_trailer[
    INITRAMFS_NEWC_TRAILER_LENGTH
] = {
    'T', 'R', 'A', 'I', 'L',
    'E', 'R', '!', '!', '!',
};

// Private functions and helpers declarations
static bool initramfs_format_range_fits(
    size_t offset,
    size_t length,
    size_t total
);

static bool initramfs_format_align_up(
    size_t value,
    size_t alignment,
    size_t *result
);

static bool initramfs_format_magic_valid(
    const uint8_t *header
);

static bool initramfs_format_hex_digit(
    uint8_t character,
    uint32_t *digit
);

static bool initramfs_format_hex_u32(
    const uint8_t *text,
    uint32_t *result
);

static bool initramfs_format_header_fields(
    const uint8_t *header,
    uint32_t fields[INITRAMFS_NEWC_FIELD_COUNT]
);

static bool initramfs_format_name_terminated(
    const uint8_t *name,
    size_t name_size
);

static bool initramfs_format_component_reserved(
    const char *component,
    size_t component_length
);

static bool initramfs_format_path_valid(
    const char *path,
    size_t path_length
);

static bool initramfs_format_path_equal(
    const char *path,
    size_t path_length,
    const char *expected,
    size_t expected_length
);

static bool initramfs_format_padding_zero(
    const uint8_t *archive,
    size_t start,
    size_t end
);

static enum initramfs_format_result initramfs_format_fail(
    struct initramfs_format_iterator *iterator,
    enum initramfs_format_result result
);

// Public functions implementations
bool initramfs_format_iterator_initialize(
    const void *archive,
    size_t archive_size,
    struct initramfs_format_iterator *iterator)
{
    if (
        archive == NULL ||
        iterator == NULL
    ) {
        return false;
    }

    iterator->archive =
        archive;

    iterator->archive_size =
        archive_size;

    iterator->offset =
        0;

    iterator->finished =
        false;

    iterator->failed =
        false;

    return true;
}

enum initramfs_format_result initramfs_format_next(
    struct initramfs_format_iterator *iterator,
    struct initramfs_format_entry *entry)
{
    if (
        iterator == NULL ||
        entry == NULL
    ) {
        return
            INITRAMFS_FORMAT_RESULT_INVALID_ARGUMENT;
    }

    if (
        iterator->archive == NULL
    ) {
        return
            INITRAMFS_FORMAT_RESULT_INVALID_ARGUMENT;
    }

    if (iterator->failed) {
        return
            INITRAMFS_FORMAT_RESULT_MALFORMED;
    }

    if (iterator->finished) {
        return
            INITRAMFS_FORMAT_RESULT_END;
    }

    size_t record_offset =
        iterator->offset;

    if (
        (record_offset &
         (INITRAMFS_NEWC_ALIGNMENT - 1U)) != 0
    ) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    if (!initramfs_format_range_fits(
        record_offset,
        INITRAMFS_NEWC_HEADER_SIZE,
        iterator->archive_size
    )) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    const uint8_t *header =
        iterator->archive +
        record_offset;

    if (!initramfs_format_magic_valid(
        header
    )) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    uint32_t fields[
        INITRAMFS_NEWC_FIELD_COUNT
    ];

    if (!initramfs_format_header_fields(
        header,
        fields
    )) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    /*
     * Plain newc archives require c_check to remain zero. The CRC variant
     * uses another magic and is deliberately not accepted by this parser.
     */
    if (
        fields[
            INITRAMFS_NEWC_FIELD_CHECK
        ] != 0
    ) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    size_t name_size =
        (size_t) fields[
            INITRAMFS_NEWC_FIELD_NAME_SIZE
        ];

    if (
        name_size == 0 ||
        name_size >
            (size_t) VFS_PATH_MAX + 1U
    ) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    size_t name_offset =
        record_offset +
        INITRAMFS_NEWC_HEADER_SIZE;

    if (!initramfs_format_range_fits(
        name_offset,
        name_size,
        iterator->archive_size
    )) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    const uint8_t *name =
        iterator->archive +
        name_offset;

    if (!initramfs_format_name_terminated(
        name,
        name_size
    )) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    size_t path_length =
        name_size - 1U;

    size_t name_end =
        name_offset +
        name_size;

    size_t data_offset;

    if (!initramfs_format_align_up(
        name_end,
        INITRAMFS_NEWC_ALIGNMENT,
        &data_offset
    )) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    if (data_offset > iterator->archive_size) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    size_t file_size =
        (size_t) fields[
            INITRAMFS_NEWC_FIELD_FILE_SIZE
        ];

    if (!initramfs_format_range_fits(
        data_offset,
        file_size,
        iterator->archive_size
    )) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    size_t data_end =
        data_offset +
        file_size;

    size_t next_offset;

    if (!initramfs_format_align_up(
        data_end,
        INITRAMFS_NEWC_ALIGNMENT,
        &next_offset
    )) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    if (next_offset > iterator->archive_size) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    const char *path =
        (const char *) name;

    /*
     * TRAILER!!! is archive structure, not a filesystem pathname.
     */
    if (initramfs_format_path_equal(
        path,
        path_length,
        initramfs_newc_trailer,
        INITRAMFS_NEWC_TRAILER_LENGTH
    )) {
        if (file_size != 0) {
            return initramfs_format_fail(
                iterator,
                INITRAMFS_FORMAT_RESULT_MALFORMED
            );
        }

        /*
         * Accept conventional zero padding after the trailer but reject
         * unrelated bytes hidden after the logical end of the archive.
         */
        if (!initramfs_format_padding_zero(
            iterator->archive,
            next_offset,
            iterator->archive_size
        )) {
            return initramfs_format_fail(
                iterator,
                INITRAMFS_FORMAT_RESULT_MALFORMED
            );
        }

        iterator->offset =
            iterator->archive_size;

        iterator->finished =
            true;

        return
            INITRAMFS_FORMAT_RESULT_END;
    }

    if (!initramfs_format_path_valid(
        path,
        path_length
    )) {
        return initramfs_format_fail(
            iterator,
            INITRAMFS_FORMAT_RESULT_MALFORMED
        );
    }

    uint32_t mode =
        fields[
            INITRAMFS_NEWC_FIELD_MODE
        ];

    uint32_t object_type =
        mode &
        INITRAMFS_NEWC_MODE_TYPE_MASK;

    struct initramfs_format_entry candidate = {
        .path =
            path,
        .path_length =
            path_length,
        .data =
            file_size == 0
                ? NULL
                : iterator->archive +
                    data_offset,
        .size =
            file_size,
    };

    switch (object_type) {
        case INITRAMFS_NEWC_MODE_DIRECTORY:
            if (file_size != 0) {
                return initramfs_format_fail(
                    iterator,
                    INITRAMFS_FORMAT_RESULT_MALFORMED
                );
            }

            candidate.type =
                INITRAMFS_FORMAT_ENTRY_DIRECTORY;

            break;

        case INITRAMFS_NEWC_MODE_REGULAR:
            candidate.type =
                INITRAMFS_FORMAT_ENTRY_REGULAR_FILE;

            break;

        default:
            iterator->failed =
                true;

            return
                INITRAMFS_FORMAT_RESULT_UNSUPPORTED_TYPE;
    }

    iterator->offset =
        next_offset;

    *entry =
        candidate;

    return
        INITRAMFS_FORMAT_RESULT_ENTRY;
}

// Private functions and helpers implementations
static bool initramfs_format_range_fits(
    size_t offset,
    size_t length,
    size_t total)
{
    return
        offset <= total &&
        length <= total - offset;
}

static bool initramfs_format_align_up(
    size_t value,
    size_t alignment,
    size_t *result)
{
    if (
        alignment == 0 ||
        result == NULL
    ) {
        return false;
    }

    size_t remainder =
        value % alignment;

    if (remainder == 0) {
        *result =
            value;

        return true;
    }

    size_t adjustment =
        alignment -
        remainder;

    if (value > SIZE_MAX - adjustment) {
        return false;
    }

    *result =
        value +
        adjustment;

    return true;
}

static bool initramfs_format_magic_valid(
    const uint8_t *header)
{
    if (header == NULL) {
        return false;
    }

    for (
        size_t index = 0;
        index < INITRAMFS_NEWC_MAGIC_SIZE;
        ++index
    ) {
        if (
            header[index] !=
            initramfs_newc_magic[index]
        ) {
            return false;
        }
    }

    return true;
}

static bool initramfs_format_hex_digit(
    uint8_t character,
    uint32_t *digit)
{
    if (digit == NULL) {
        return false;
    }

    if (
        character >= '0' &&
        character <= '9'
    ) {
        *digit =
            (uint32_t) (
                character - '0'
            );

        return true;
    }

    if (
        character >= 'a' &&
        character <= 'f'
    ) {
        *digit =
            (uint32_t) (
                character - 'a'
            ) +
            10U;

        return true;
    }

    if (
        character >= 'A' &&
        character <= 'F'
    ) {
        *digit =
            (uint32_t) (
                character - 'A'
            ) +
            10U;

        return true;
    }

    return false;
}

static bool initramfs_format_hex_u32(
    const uint8_t *text,
    uint32_t *result)
{
    if (
        text == NULL ||
        result == NULL
    ) {
        return false;
    }

    uint32_t value =
        0;

    for (
        size_t index = 0;
        index < INITRAMFS_NEWC_FIELD_SIZE;
        ++index
    ) {
        uint32_t digit;

        if (!initramfs_format_hex_digit(
            text[index],
            &digit
        )) {
            return false;
        }

        value =
            (value << 4U) |
            digit;
    }

    *result =
        value;

    return true;
}

static bool initramfs_format_header_fields(
    const uint8_t *header,
    uint32_t fields[INITRAMFS_NEWC_FIELD_COUNT])
{
    if (
        header == NULL ||
        fields == NULL
    ) {
        return false;
    }

    for (
        size_t field = 0;
        field < INITRAMFS_NEWC_FIELD_COUNT;
        ++field
    ) {
        const uint8_t *text =
            header +
            INITRAMFS_NEWC_MAGIC_SIZE +
            field *
                INITRAMFS_NEWC_FIELD_SIZE;

        if (!initramfs_format_hex_u32(
            text,
            &fields[field]
        )) {
            return false;
        }
    }

    return true;
}

static bool initramfs_format_name_terminated(
    const uint8_t *name,
    size_t name_size)
{
    if (
        name == NULL ||
        name_size == 0
    ) {
        return false;
    }

    if (
        name[name_size - 1U] != '\0'
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index + 1U < name_size;
        ++index
    ) {
        if (name[index] == '\0') {
            return false;
        }
    }

    return true;
}

static bool initramfs_format_component_reserved(
    const char *component,
    size_t component_length)
{
    if (component == NULL) {
        return true;
    }

    if (
        component_length == 1 &&
        component[0] == '.'
    ) {
        return true;
    }

    return
        component_length == 2 &&
        component[0] == '.' &&
        component[1] == '.';
}

static bool initramfs_format_path_valid(
    const char *path,
    size_t path_length)
{
    if (
        path == NULL ||
        path_length == 0 ||
        path_length > VFS_PATH_MAX
    ) {
        return false;
    }

    /*
     * Archive entries use canonical relative paths. The VFS later supplies
     * namespace-root semantics; the archive itself must not encode them.
     */
    if (
        path[0] == '/' ||
        path[path_length - 1U] == '/'
    ) {
        return false;
    }

    size_t component_start =
        0;

    size_t component_length =
        0;

    for (
        size_t index = 0;
        index < path_length;
        ++index
    ) {
        if (path[index] == '\0') {
            return false;
        }

        if (path[index] != '/') {
            ++component_length;

            if (
                component_length >
                VFS_NAME_MAX
            ) {
                return false;
            }

            continue;
        }

        if (
            component_length == 0 ||
            initramfs_format_component_reserved(
                path + component_start,
                component_length
            )
        ) {
            return false;
        }

        component_start =
            index + 1U;

        component_length =
            0;
    }

    if (
        component_length == 0 ||
        initramfs_format_component_reserved(
            path + component_start,
            component_length
        )
    ) {
        return false;
    }

    return true;
}

static bool initramfs_format_path_equal(
    const char *path,
    size_t path_length,
    const char *expected,
    size_t expected_length)
{
    if (
        path == NULL ||
        expected == NULL ||
        path_length != expected_length
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < path_length;
        ++index
    ) {
        if (
            path[index] !=
            expected[index]
        ) {
            return false;
        }
    }

    return true;
}

static bool initramfs_format_padding_zero(
    const uint8_t *archive,
    size_t start,
    size_t end)
{
    if (
        archive == NULL ||
        start > end
    ) {
        return false;
    }

    for (
        size_t index = start;
        index < end;
        ++index
    ) {
        if (archive[index] != 0) {
            return false;
        }
    }

    return true;
}

static enum initramfs_format_result initramfs_format_fail(
    struct initramfs_format_iterator *iterator,
    enum initramfs_format_result result)
{
    if (iterator != NULL) {
        iterator->failed =
            true;
    }

    return result;
}

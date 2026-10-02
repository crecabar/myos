// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file initramfs_format_test.c
 * @brief Initramfs CPIO newc parser regression tests.
 */

#include "initramfs_format_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../fs/initramfs_format.h"
#include "../runtime/memory.h"

#include <stddef.h>
#include <stdint.h>

// Defines
#define INITRAMFS_FORMAT_TEST_CAPACITY 2048U

#define INITRAMFS_FORMAT_TEST_HEADER_SIZE 110U
#define INITRAMFS_FORMAT_TEST_FIELD_SIZE 8U

#define INITRAMFS_FORMAT_TEST_MODE_FIELD 1U

#define INITRAMFS_FORMAT_TEST_MODE_DIRECTORY 0040755U
#define INITRAMFS_FORMAT_TEST_MODE_REGULAR 0100644U
#define INITRAMFS_FORMAT_TEST_MODE_SYMLINK 0120777U

// Private functions and helpers declarations
static void initramfs_format_test_write_hex(
    uint8_t *destination,
    uint32_t value
);

static size_t initramfs_format_test_align(
    size_t value
);

static size_t initramfs_format_test_append_entry(
    uint8_t *archive,
    size_t capacity,
    size_t offset,
    const char *name,
    size_t name_length,
    uint32_t mode,
    const uint8_t *data,
    size_t data_size
);

static size_t initramfs_format_test_append_trailer(
    uint8_t *archive,
    size_t capacity,
    size_t offset
);

static bool initramfs_format_test_text_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
);

static void initramfs_format_test_valid_archive(void);

static void initramfs_format_test_invalid_header(void);

static void initramfs_format_test_truncation(void);

static void initramfs_format_test_invalid_names(void);

static void initramfs_format_test_unsupported_type(void);

static void initramfs_format_test_directory_payload(void);

static void initramfs_format_test_missing_trailer(void);

static void initramfs_format_test_trailer_padding(void);

// Public functions implementations
void initramfs_format_test_run(void)
{
    initramfs_format_test_valid_archive();
    initramfs_format_test_invalid_header();
    initramfs_format_test_truncation();
    initramfs_format_test_invalid_names();
    initramfs_format_test_unsupported_type();
    initramfs_format_test_directory_payload();
    initramfs_format_test_missing_trailer();
    initramfs_format_test_trailer_padding();

    diagnostics_write(
        "[fs] Initramfs newc format parser tests passed\n"
    );
}

// Private functions and helpers implementations
static void initramfs_format_test_write_hex(
    uint8_t *destination,
    uint32_t value)
{
    static const char digits[] =
        "0123456789abcdef";

    if (destination == NULL) {
        kernel_panic(
            "Initramfs test received null hex destination"
        );
    }

    for (
        size_t index = 0;
        index < INITRAMFS_FORMAT_TEST_FIELD_SIZE;
        ++index
    ) {
        size_t shift =
            (
                INITRAMFS_FORMAT_TEST_FIELD_SIZE -
                index -
                1U
            ) *
            4U;

        destination[index] =
            (uint8_t) digits[
                (value >> shift) &
                0xFU
            ];
    }
}

static size_t initramfs_format_test_align(
    size_t value)
{
    return
        (value + 3U) &
        ~(size_t) 3U;
}

static size_t initramfs_format_test_append_entry(
    uint8_t *archive,
    size_t capacity,
    size_t offset,
    const char *name,
    size_t name_length,
    uint32_t mode,
    const uint8_t *data,
    size_t data_size)
{
    if (
        archive == NULL ||
        name == NULL ||
        data_size > UINT32_MAX ||
        name_length >= UINT32_MAX
    ) {
        kernel_panic(
            "Invalid initramfs test entry"
        );
    }

    size_t name_size =
        name_length + 1U;

    if (
        offset > capacity ||
        INITRAMFS_FORMAT_TEST_HEADER_SIZE >
            capacity - offset
    ) {
        kernel_panic(
            "Initramfs test archive header overflow"
        );
    }

    uint8_t *header =
        archive +
        offset;

    header[0] = '0';
    header[1] = '7';
    header[2] = '0';
    header[3] = '7';
    header[4] = '0';
    header[5] = '1';

    uint32_t fields[13] = {0};

    fields[0] = 1U;
    fields[1] = mode;
    fields[4] = 1U;
    fields[6] = (uint32_t) data_size;
    fields[11] = (uint32_t) name_size;

    for (
        size_t field = 0;
        field < 13U;
        ++field
    ) {
        initramfs_format_test_write_hex(
            header +
                6U +
                field *
                    INITRAMFS_FORMAT_TEST_FIELD_SIZE,
            fields[field]
        );
    }

    offset +=
        INITRAMFS_FORMAT_TEST_HEADER_SIZE;

    if (
        name_size >
        capacity - offset
    ) {
        kernel_panic(
            "Initramfs test archive name overflow"
        );
    }

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        archive[offset + index] =
            (uint8_t) name[index];
    }

    archive[
        offset +
        name_length
    ] = '\0';

    offset +=
        name_size;

    size_t aligned =
        initramfs_format_test_align(
            offset
        );

    if (aligned > capacity) {
        kernel_panic(
            "Initramfs test archive name padding overflow"
        );
    }

    while (offset < aligned) {
        archive[offset++] =
            0;
    }

    if (
        data_size >
        capacity - offset
    ) {
        kernel_panic(
            "Initramfs test archive data overflow"
        );
    }

    for (
        size_t index = 0;
        index < data_size;
        ++index
    ) {
        archive[offset + index] =
            data[index];
    }

    offset +=
        data_size;

    aligned =
        initramfs_format_test_align(
            offset
        );

    if (aligned > capacity) {
        kernel_panic(
            "Initramfs test archive data padding overflow"
        );
    }

    while (offset < aligned) {
        archive[offset++] =
            0;
    }

    return
        offset;
}

static size_t initramfs_format_test_append_trailer(
    uint8_t *archive,
    size_t capacity,
    size_t offset)
{
    static const char trailer[] =
        "TRAILER!!!";

    return initramfs_format_test_append_entry(
        archive,
        capacity,
        offset,
        trailer,
        sizeof(trailer) - 1U,
        0,
        NULL,
        0
    );
}

static bool initramfs_format_test_text_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length)
{
    if (
        left == NULL ||
        right == NULL ||
        left_length != right_length
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < left_length;
        ++index
    ) {
        if (
            left[index] !=
            right[index]
        ) {
            return false;
        }
    }

    return true;
}

static void initramfs_format_test_valid_archive(void)
{
    uint8_t archive[
        INITRAMFS_FORMAT_TEST_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    size_t size =
        0;

    size =
        initramfs_format_test_append_entry(
            archive,
            sizeof(archive),
            size,
            "bin",
            3,
            INITRAMFS_FORMAT_TEST_MODE_DIRECTORY,
            NULL,
            0
        );

    static const uint8_t payload[] = {
        'O', 'K',
    };

    size =
        initramfs_format_test_append_entry(
            archive,
            sizeof(archive),
            size,
            "bin/hello",
            9,
            INITRAMFS_FORMAT_TEST_MODE_REGULAR,
            payload,
            sizeof(payload)
        );

    size =
        initramfs_format_test_append_trailer(
            archive,
            sizeof(archive),
            size
        );

    struct initramfs_format_iterator iterator;

    if (!initramfs_format_iterator_initialize(
        archive,
        size,
        &iterator
    )) {
        kernel_panic(
            "Initramfs parser rejected valid iterator setup"
        );
    }

    struct initramfs_format_entry entry;

    if (
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_ENTRY ||
        entry.type !=
            INITRAMFS_FORMAT_ENTRY_DIRECTORY ||
        !initramfs_format_test_text_equal(
            entry.path,
            entry.path_length,
            "bin",
            3
        ) ||
        entry.data != NULL ||
        entry.size != 0
    ) {
        kernel_panic(
            "Initramfs parser rejected valid directory"
        );
    }

    if (
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_ENTRY ||
        entry.type !=
            INITRAMFS_FORMAT_ENTRY_REGULAR_FILE ||
        !initramfs_format_test_text_equal(
            entry.path,
            entry.path_length,
            "bin/hello",
            9
        ) ||
        entry.size != sizeof(payload) ||
        entry.data == NULL ||
        entry.data[0] != 'O' ||
        entry.data[1] != 'K'
    ) {
        kernel_panic(
            "Initramfs parser rejected valid regular file"
        );
    }

    if (
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_END ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_END
    ) {
        kernel_panic(
            "Initramfs parser trailer semantics mismatch"
        );
    }
}

static void initramfs_format_test_invalid_header(void)
{
    uint8_t archive[
        INITRAMFS_FORMAT_TEST_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    size_t size =
        initramfs_format_test_append_trailer(
            archive,
            sizeof(archive),
            0
        );

    struct initramfs_format_iterator iterator;
    struct initramfs_format_entry entry;

    archive[0] =
        '1';

    if (
        !initramfs_format_iterator_initialize(
            archive,
            size,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser accepted invalid magic"
        );
    }

    memset(
        archive,
        0,
        sizeof(archive)
    );

    size =
        initramfs_format_test_append_trailer(
            archive,
            sizeof(archive),
            0
        );

    archive[
        6U +
        INITRAMFS_FORMAT_TEST_MODE_FIELD *
            INITRAMFS_FORMAT_TEST_FIELD_SIZE
    ] = 'g';

    if (
        !initramfs_format_iterator_initialize(
            archive,
            size,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser accepted invalid hexadecimal field"
        );
    }
}

static void initramfs_format_test_truncation(void)
{
    uint8_t archive[
        INITRAMFS_FORMAT_TEST_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    static const uint8_t payload[] = {
        0x11, 0x22, 0x33, 0x44,
    };

    size_t size =
        initramfs_format_test_append_entry(
            archive,
            sizeof(archive),
            0,
            "file",
            4,
            INITRAMFS_FORMAT_TEST_MODE_REGULAR,
            payload,
            sizeof(payload)
        );

    struct initramfs_format_iterator iterator;
    struct initramfs_format_entry entry;

    if (
        !initramfs_format_iterator_initialize(
            archive,
            INITRAMFS_FORMAT_TEST_HEADER_SIZE - 1U,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser accepted truncated header"
        );
    }

    if (
        !initramfs_format_iterator_initialize(
            archive,
            INITRAMFS_FORMAT_TEST_HEADER_SIZE + 2U,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser accepted truncated name"
        );
    }

    if (
        size < 2U ||
        !initramfs_format_iterator_initialize(
            archive,
            size - 2U,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser accepted truncated record"
        );
    }
}

static void initramfs_format_test_invalid_names(void)
{
    static const char *invalid_paths[] = {
        "/bin",
        "bin/",
        "bin//file",
        ".",
        "..",
        "bin/./file",
        "bin/../file",
    };

    static const size_t invalid_lengths[] = {
        4,
        4,
        9,
        1,
        2,
        10,
        11,
    };

    for (
        size_t test = 0;
        test <
            sizeof(invalid_paths) /
            sizeof(invalid_paths[0]);
        ++test
    ) {
        uint8_t archive[
            INITRAMFS_FORMAT_TEST_CAPACITY
        ];

        memset(
            archive,
            0,
            sizeof(archive)
        );

        size_t size =
            initramfs_format_test_append_entry(
                archive,
                sizeof(archive),
                0,
                invalid_paths[test],
                invalid_lengths[test],
                INITRAMFS_FORMAT_TEST_MODE_REGULAR,
                NULL,
                0
            );

        size =
            initramfs_format_test_append_trailer(
                archive,
                sizeof(archive),
                size
            );

        struct initramfs_format_iterator iterator;
        struct initramfs_format_entry entry;

        if (
            !initramfs_format_iterator_initialize(
                archive,
                size,
                &iterator
            ) ||
            initramfs_format_next(
                &iterator,
                &entry
            ) != INITRAMFS_FORMAT_RESULT_MALFORMED
        ) {
            kernel_panic(
                "Initramfs parser accepted invalid pathname"
            );
        }
    }

    /*
     * Missing final NUL.
     */
    uint8_t archive[
        INITRAMFS_FORMAT_TEST_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    size_t size =
        initramfs_format_test_append_entry(
            archive,
            sizeof(archive),
            0,
            "file",
            4,
            INITRAMFS_FORMAT_TEST_MODE_REGULAR,
            NULL,
            0
        );

    archive[
        INITRAMFS_FORMAT_TEST_HEADER_SIZE +
        4U
    ] = 'x';

    struct initramfs_format_iterator iterator;
    struct initramfs_format_entry entry;

    if (
        !initramfs_format_iterator_initialize(
            archive,
            size,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser accepted unterminated pathname"
        );
    }

    /*
     * Embedded NUL before the declared terminator.
     */
    memset(
        archive,
        0,
        sizeof(archive)
    );

    size =
        initramfs_format_test_append_entry(
            archive,
            sizeof(archive),
            0,
            "file",
            4,
            INITRAMFS_FORMAT_TEST_MODE_REGULAR,
            NULL,
            0
        );

    archive[
        INITRAMFS_FORMAT_TEST_HEADER_SIZE +
        1U
    ] = '\0';

    if (
        !initramfs_format_iterator_initialize(
            archive,
            size,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser accepted embedded pathname NUL"
        );
    }
}

static void initramfs_format_test_unsupported_type(void)
{
    uint8_t archive[
        INITRAMFS_FORMAT_TEST_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    size_t size =
        initramfs_format_test_append_entry(
            archive,
            sizeof(archive),
            0,
            "link",
            4,
            INITRAMFS_FORMAT_TEST_MODE_SYMLINK,
            NULL,
            0
        );

    size =
        initramfs_format_test_append_trailer(
            archive,
            sizeof(archive),
            size
        );

    struct initramfs_format_iterator iterator;
    struct initramfs_format_entry entry;

    if (
        !initramfs_format_iterator_initialize(
            archive,
            size,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_UNSUPPORTED_TYPE
    ) {
        kernel_panic(
            "Initramfs parser accepted unsupported object type"
        );
    }

    /*
     * An archive-format failure permanently poisons the iterator. It must not
     * resume parsing after the unsupported entry.
     */
    if (
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser resumed after unsupported type"
        );
    }
}

static void initramfs_format_test_directory_payload(void)
{
    uint8_t archive[
        INITRAMFS_FORMAT_TEST_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    static const uint8_t payload[] = {
        0x5a,
    };

    size_t size =
        initramfs_format_test_append_entry(
            archive,
            sizeof(archive),
            0,
            "dir",
            3,
            INITRAMFS_FORMAT_TEST_MODE_DIRECTORY,
            payload,
            sizeof(payload)
        );

    size =
        initramfs_format_test_append_trailer(
            archive,
            sizeof(archive),
            size
        );

    struct initramfs_format_iterator iterator;
    struct initramfs_format_entry entry;

    if (
        !initramfs_format_iterator_initialize(
            archive,
            size,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser accepted directory payload"
        );
    }
}

static void initramfs_format_test_missing_trailer(void)
{
    uint8_t archive[
        INITRAMFS_FORMAT_TEST_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    size_t size =
        initramfs_format_test_append_entry(
            archive,
            sizeof(archive),
            0,
            "file",
            4,
            INITRAMFS_FORMAT_TEST_MODE_REGULAR,
            NULL,
            0
        );

    struct initramfs_format_iterator iterator;
    struct initramfs_format_entry entry;

    if (
        !initramfs_format_iterator_initialize(
            archive,
            size,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_ENTRY ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser accepted missing trailer"
        );
    }
}

static void initramfs_format_test_trailer_padding(void)
{
    uint8_t archive[
        INITRAMFS_FORMAT_TEST_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    size_t trailer_end =
        initramfs_format_test_append_trailer(
            archive,
            sizeof(archive),
            0
        );

    struct initramfs_format_iterator iterator;
    struct initramfs_format_entry entry;

    if (
        !initramfs_format_iterator_initialize(
            archive,
            trailer_end + 8U,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_END
    ) {
        kernel_panic(
            "Initramfs parser rejected zero trailer padding"
        );
    }

    archive[
        trailer_end + 3U
    ] = 0xa5U;

    if (
        !initramfs_format_iterator_initialize(
            archive,
            trailer_end + 8U,
            &iterator
        ) ||
        initramfs_format_next(
            &iterator,
            &entry
        ) != INITRAMFS_FORMAT_RESULT_MALFORMED
    ) {
        kernel_panic(
            "Initramfs parser accepted bytes after trailer"
        );
    }
}

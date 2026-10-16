// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file vfs_directory_test.c
 * @brief VFS directory enumeration contract tests.
 */

#include "vfs_directory_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../vfs/vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum vfs_directory_test_stream_mode {
    VFS_DIRECTORY_TEST_STREAM_NORMAL,
    VFS_DIRECTORY_TEST_STREAM_FORCED_RESULT,
    VFS_DIRECTORY_TEST_STREAM_MALFORMED,
    VFS_DIRECTORY_TEST_STREAM_MUTATE_OFFSET,
};

enum vfs_directory_test_malformed_kind {
    VFS_DIRECTORY_TEST_MALFORMED_EMPTY_NAME,
    VFS_DIRECTORY_TEST_MALFORMED_NAME_TOO_LONG,
    VFS_DIRECTORY_TEST_MALFORMED_MISSING_TERMINATOR,
    VFS_DIRECTORY_TEST_MALFORMED_EMBEDDED_NUL,
    VFS_DIRECTORY_TEST_MALFORMED_SLASH,
    VFS_DIRECTORY_TEST_MALFORMED_DOT,
    VFS_DIRECTORY_TEST_MALFORMED_DOT_DOT,
    VFS_DIRECTORY_TEST_MALFORMED_NODE_TYPE,
};

struct vfs_directory_test_expected_entry {
    enum vfs_node_type type;

    const char *name;

    size_t name_length;
};

struct vfs_directory_test_stream {
    enum vfs_directory_test_stream_mode mode;

    size_t cursor;

    size_t call_count;

    enum vfs_directory_read_result forced_result;

    enum vfs_directory_test_malformed_kind malformed_kind;
};

// Static local variables
static const struct vfs_directory_test_expected_entry
    vfs_directory_test_entries[] = {
        {
            .type =
                VFS_NODE_TYPE_REGULAR_FILE,
            .name =
                "alpha",
            .name_length =
                sizeof("alpha") - 1U,
        },
        {
            .type =
                VFS_NODE_TYPE_DIRECTORY,
            .name =
                "devices",
            .name_length =
                sizeof("devices") - 1U,
        },
        {
            .type =
                VFS_NODE_TYPE_CHARACTER_DEVICE,
            .name =
                "console",
            .name_length =
                sizeof("console") - 1U,
        },
    };

// Private functions and helpers declarations
static enum vfs_directory_read_result
vfs_directory_test_read_callback(
    struct vfs_file *file,
    struct vfs_directory_entry *result
);

static void vfs_directory_test_copy_name(
    struct vfs_directory_entry *result,
    const char *name,
    size_t name_length
);

static void vfs_directory_test_write_valid_entry(
    struct vfs_directory_entry *result
);

static void vfs_directory_test_write_malformed_entry(
    enum vfs_directory_test_malformed_kind kind,
    struct vfs_directory_entry *result
);

static void vfs_directory_test_set_sentinel(
    struct vfs_directory_entry *entry
);

static bool vfs_directory_test_is_sentinel(
    const struct vfs_directory_entry *entry
);

static bool vfs_directory_test_entry_matches(
    const struct vfs_directory_entry *entry,
    const struct vfs_directory_test_expected_entry *expected
);

static void vfs_directory_test_successful_enumeration(void);

static void vfs_directory_test_independent_cursors(void);

static void vfs_directory_test_frontend_validation(void);

static void vfs_directory_test_backend_result_propagation(void);

static void vfs_directory_test_malformed_entries(void);

static void vfs_directory_test_callback_violations(void);

// Static local variables
static const struct vfs_file_operations
    vfs_directory_test_file_operations = {
        .read_directory =
            vfs_directory_test_read_callback,
        .destroy =
            NULL,
    };

static const struct vfs_file_operations
    vfs_directory_test_empty_file_operations = {
        .read =
            NULL,
        .write =
            NULL,
        .read_directory =
            NULL,
        .destroy =
            NULL,
    };

// Public functions implementations
void vfs_directory_test_run(void)
{
    vfs_directory_test_successful_enumeration();

    vfs_directory_test_independent_cursors();

    vfs_directory_test_frontend_validation();

    vfs_directory_test_backend_result_propagation();

    vfs_directory_test_malformed_entries();

    vfs_directory_test_callback_violations();

    diagnostics_write(
        "[vfs] Directory enumeration contract tests passed\n"
    );
}

// Private functions and helpers implementations
static enum vfs_directory_read_result
vfs_directory_test_read_callback(
    struct vfs_file *file,
    struct vfs_directory_entry *result)
{
    if (
        file == NULL ||
        result == NULL ||
        file->private_data == NULL
    ) {
        return
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_directory_test_stream *stream =
        file->private_data;

    ++stream->call_count;

    switch (stream->mode) {
        case VFS_DIRECTORY_TEST_STREAM_NORMAL:
            if (
                stream->cursor >=
                    sizeof(vfs_directory_test_entries) /
                    sizeof(vfs_directory_test_entries[0])
            ) {
                return
                    VFS_DIRECTORY_READ_RESULT_END;
            }

            result->type =
                vfs_directory_test_entries[
                    stream->cursor
                ].type;

            vfs_directory_test_copy_name(
                result,
                vfs_directory_test_entries[
                    stream->cursor
                ].name,
                vfs_directory_test_entries[
                    stream->cursor
                ].name_length
            );

            ++stream->cursor;

            return
                VFS_DIRECTORY_READ_RESULT_ENTRY;

        case VFS_DIRECTORY_TEST_STREAM_FORCED_RESULT:
            /*
             * Deliberately populate callback output even when returning an
             * error. The generic VFS must isolate this storage from its caller.
             */
            vfs_directory_test_write_valid_entry(
                result
            );

            return
                stream->forced_result;

        case VFS_DIRECTORY_TEST_STREAM_MALFORMED:
            vfs_directory_test_write_malformed_entry(
                stream->malformed_kind,
                result
            );

            return
                VFS_DIRECTORY_READ_RESULT_ENTRY;

        case VFS_DIRECTORY_TEST_STREAM_MUTATE_OFFSET:
            vfs_directory_test_write_valid_entry(
                result
            );

            ++file->offset;

            return
                VFS_DIRECTORY_READ_RESULT_ENTRY;
    }

    return
        VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
}

static void vfs_directory_test_copy_name(
    struct vfs_directory_entry *result,
    const char *name,
    size_t name_length)
{
    if (
        result == NULL ||
        name == NULL ||
        name_length > VFS_NAME_MAX
    ) {
        kernel_panic(
            "VFS directory test name copy received invalid input"
        );
    }

    result->name_length =
        name_length;

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        result->name[index] =
            name[index];
    }

    result->name[name_length] =
        '\0';
}

static void vfs_directory_test_write_valid_entry(
    struct vfs_directory_entry *result)
{
    if (result == NULL) {
        kernel_panic(
            "VFS directory test valid entry received NULL"
        );
    }

    result->type =
        VFS_NODE_TYPE_REGULAR_FILE;

    vfs_directory_test_copy_name(
        result,
        "backend",
        sizeof("backend") - 1U
    );
}

static void vfs_directory_test_write_malformed_entry(
    enum vfs_directory_test_malformed_kind kind,
    struct vfs_directory_entry *result)
{
    if (result == NULL) {
        kernel_panic(
            "VFS directory malformed fixture received NULL"
        );
    }

    result->type =
        VFS_NODE_TYPE_REGULAR_FILE;

    switch (kind) {
        case VFS_DIRECTORY_TEST_MALFORMED_EMPTY_NAME:
            result->name_length =
                0;

            result->name[0] =
                '\0';

            return;

        case VFS_DIRECTORY_TEST_MALFORMED_NAME_TOO_LONG:
            result->name_length =
                VFS_NAME_MAX + 1U;

            return;

        case VFS_DIRECTORY_TEST_MALFORMED_MISSING_TERMINATOR:
            result->name_length =
                1;

            result->name[0] =
                'a';

            result->name[1] =
                'x';

            return;

        case VFS_DIRECTORY_TEST_MALFORMED_EMBEDDED_NUL:
            result->name_length =
                3;

            result->name[0] =
                'a';

            result->name[1] =
                '\0';

            result->name[2] =
                'b';

            result->name[3] =
                '\0';

            return;

        case VFS_DIRECTORY_TEST_MALFORMED_SLASH:
            result->name_length =
                3;

            result->name[0] =
                'a';

            result->name[1] =
                '/';

            result->name[2] =
                'b';

            result->name[3] =
                '\0';

            return;

        case VFS_DIRECTORY_TEST_MALFORMED_DOT:
            result->name_length =
                1;

            result->name[0] =
                '.';

            result->name[1] =
                '\0';

            return;

        case VFS_DIRECTORY_TEST_MALFORMED_DOT_DOT:
            result->name_length =
                2;

            result->name[0] =
                '.';

            result->name[1] =
                '.';

            result->name[2] =
                '\0';

            return;

        case VFS_DIRECTORY_TEST_MALFORMED_NODE_TYPE:
            result->type =
                (enum vfs_node_type) 99;

            vfs_directory_test_copy_name(
                result,
                "invalid-type",
                sizeof("invalid-type") - 1U
            );

            return;
    }

    kernel_panic(
        "VFS directory malformed fixture received unknown kind"
    );
}

static void vfs_directory_test_set_sentinel(
    struct vfs_directory_entry *entry)
{
    if (entry == NULL) {
        kernel_panic(
            "VFS directory sentinel received NULL"
        );
    }

    entry->type =
        VFS_NODE_TYPE_CHARACTER_DEVICE;

    entry->name_length =
        sizeof("sentinel") - 1U;

    entry->name[0] =
        's';

    entry->name[1] =
        'e';

    entry->name[2] =
        'n';

    entry->name[3] =
        't';

    entry->name[4] =
        'i';

    entry->name[5] =
        'n';

    entry->name[6] =
        'e';

    entry->name[7] =
        'l';

    entry->name[8] =
        '\0';
}

static bool vfs_directory_test_is_sentinel(
    const struct vfs_directory_entry *entry)
{
    if (
        entry == NULL ||
        entry->type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        entry->name_length !=
            sizeof("sentinel") - 1U
    ) {
        return false;
    }

    static const char sentinel[] =
        "sentinel";

    for (
        size_t index = 0;
        index < sizeof("sentinel");
        ++index
    ) {
        if (
            entry->name[index] !=
                sentinel[index]
        ) {
            return false;
        }
    }

    return true;
}

static bool vfs_directory_test_entry_matches(
    const struct vfs_directory_entry *entry,
    const struct vfs_directory_test_expected_entry *expected)
{
    if (
        entry == NULL ||
        expected == NULL ||
        entry->type !=
            expected->type ||
        entry->name_length !=
            expected->name_length
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < expected->name_length;
        ++index
    ) {
        if (
            entry->name[index] !=
                expected->name[index]
        ) {
            return false;
        }
    }

    return
        entry->name[
            expected->name_length
        ] == '\0';
}

static void vfs_directory_test_successful_enumeration(void)
{
    struct vfs_node directory;

    struct vfs_file file;

    struct vfs_directory_test_stream stream = {
        .mode =
            VFS_DIRECTORY_TEST_STREAM_NORMAL,
        .cursor =
            0,
        .call_count =
            0,
        .forced_result =
            VFS_DIRECTORY_READ_RESULT_END,
        .malformed_kind =
            VFS_DIRECTORY_TEST_MALFORMED_EMPTY_NAME,
    };

    if (
        !vfs_node_initialize(
            &directory,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &file,
            &directory,
            VFS_OPEN_ACCESS_READ,
            &vfs_directory_test_file_operations,
            &stream
        )
    ) {
        kernel_panic(
            "VFS directory enumeration fixture initialization failed"
        );
    }

    file.offset =
        37;

    for (
        size_t index = 0;
        index <
            sizeof(vfs_directory_test_entries) /
            sizeof(vfs_directory_test_entries[0]);
        ++index
    ) {
        struct vfs_directory_entry entry;

        vfs_directory_test_set_sentinel(
            &entry
        );

        if (
            vfs_file_read_directory(
                &file,
                &entry
            ) !=
                VFS_DIRECTORY_READ_RESULT_ENTRY ||
            !vfs_directory_test_entry_matches(
                &entry,
                &vfs_directory_test_entries[index]
            ) ||
            file.offset != 37
        ) {
            kernel_panic(
                "VFS directory successful enumeration failed"
            );
        }
    }

    struct vfs_directory_entry end_result;

    vfs_directory_test_set_sentinel(
        &end_result
    );

    if (
        vfs_file_read_directory(
            &file,
            &end_result
        ) !=
            VFS_DIRECTORY_READ_RESULT_END ||
        !vfs_directory_test_is_sentinel(
            &end_result
        ) ||
        file.offset != 37 ||
        stream.cursor !=
            sizeof(vfs_directory_test_entries) /
            sizeof(vfs_directory_test_entries[0]) ||
        stream.call_count != 4
    ) {
        kernel_panic(
            "VFS directory end-of-stream contract failed"
        );
    }

    if (
        !vfs_file_release(
            &file
        ) ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "VFS directory enumeration fixture cleanup failed"
        );
    }
}

static void vfs_directory_test_independent_cursors(void)
{
    struct vfs_node directory;

    struct vfs_file first_file;
    struct vfs_file second_file;

    struct vfs_directory_test_stream first_stream = {
        .mode =
            VFS_DIRECTORY_TEST_STREAM_NORMAL,
    };

    struct vfs_directory_test_stream second_stream = {
        .mode =
            VFS_DIRECTORY_TEST_STREAM_NORMAL,
    };

    if (
        !vfs_node_initialize(
            &directory,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &first_file,
            &directory,
            VFS_OPEN_ACCESS_READ,
            &vfs_directory_test_file_operations,
            &first_stream
        ) ||
        !vfs_file_initialize(
            &second_file,
            &directory,
            VFS_OPEN_ACCESS_READ,
            &vfs_directory_test_file_operations,
            &second_stream
        )
    ) {
        kernel_panic(
            "VFS directory independent cursor fixture failed"
        );
    }

    struct vfs_directory_entry entry;

    if (
        vfs_file_read_directory(
            &first_file,
            &entry
        ) !=
            VFS_DIRECTORY_READ_RESULT_ENTRY ||
        !vfs_directory_test_entry_matches(
            &entry,
            &vfs_directory_test_entries[0]
        ) ||
        vfs_file_read_directory(
            &first_file,
            &entry
        ) !=
            VFS_DIRECTORY_READ_RESULT_ENTRY ||
        !vfs_directory_test_entry_matches(
            &entry,
            &vfs_directory_test_entries[1]
        ) ||
        vfs_file_read_directory(
            &second_file,
            &entry
        ) !=
            VFS_DIRECTORY_READ_RESULT_ENTRY ||
        !vfs_directory_test_entry_matches(
            &entry,
            &vfs_directory_test_entries[0]
        ) ||
        first_stream.cursor != 2 ||
        second_stream.cursor != 1
    ) {
        kernel_panic(
            "VFS directory open-file cursor isolation failed"
        );
    }

    if (
        !vfs_file_release(
            &first_file
        ) ||
        !vfs_file_release(
            &second_file
        ) ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "VFS directory independent cursor cleanup failed"
        );
    }
}

static void vfs_directory_test_frontend_validation(void)
{
    struct vfs_directory_entry result;

    vfs_directory_test_set_sentinel(
        &result
    );

    if (
        vfs_file_read_directory(
            NULL,
            &result
        ) !=
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT ||
        !vfs_directory_test_is_sentinel(
            &result
        )
    ) {
        kernel_panic(
            "VFS directory NULL-file validation failed"
        );
    }

    struct vfs_node directory;
    struct vfs_file readable_file;

    struct vfs_directory_test_stream stream = {
        .mode =
            VFS_DIRECTORY_TEST_STREAM_NORMAL,
    };

    if (
        !vfs_node_initialize(
            &directory,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &readable_file,
            &directory,
            VFS_OPEN_ACCESS_READ,
            &vfs_directory_test_file_operations,
            &stream
        )
    ) {
        kernel_panic(
            "VFS directory validation fixture failed"
        );
    }

    if (
        vfs_file_read_directory(
            &readable_file,
            NULL
        ) !=
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "VFS directory NULL-result validation failed"
        );
    }

    struct vfs_node regular;
    struct vfs_file regular_file;

    if (
        !vfs_node_initialize(
            &regular,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &regular_file,
            &regular,
            VFS_OPEN_ACCESS_READ,
            &vfs_directory_test_file_operations,
            &stream
        )
    ) {
        kernel_panic(
            "VFS directory non-directory fixture failed"
        );
    }

    vfs_directory_test_set_sentinel(
        &result
    );

    if (
        vfs_file_read_directory(
            &regular_file,
            &result
        ) !=
            VFS_DIRECTORY_READ_RESULT_NOT_DIRECTORY ||
        !vfs_directory_test_is_sentinel(
            &result
        )
    ) {
        kernel_panic(
            "VFS directory non-directory rejection failed"
        );
    }

    struct vfs_file write_only_file;

    if (!vfs_file_initialize(
        &write_only_file,
        &directory,
        VFS_OPEN_ACCESS_WRITE,
        &vfs_directory_test_file_operations,
        &stream
    )) {
        kernel_panic(
            "VFS directory write-only fixture failed"
        );
    }

    vfs_directory_test_set_sentinel(
        &result
    );

    if (
        vfs_file_read_directory(
            &write_only_file,
            &result
        ) !=
            VFS_DIRECTORY_READ_RESULT_ACCESS_DENIED ||
        !vfs_directory_test_is_sentinel(
            &result
        )
    ) {
        kernel_panic(
            "VFS directory read-access enforcement failed"
        );
    }

    struct vfs_file no_operations_file;

    if (!vfs_file_initialize(
        &no_operations_file,
        &directory,
        VFS_OPEN_ACCESS_READ,
        NULL,
        &stream
    )) {
        kernel_panic(
            "VFS directory no-operations fixture failed"
        );
    }

    vfs_directory_test_set_sentinel(
        &result
    );

    if (
        vfs_file_read_directory(
            &no_operations_file,
            &result
        ) !=
            VFS_DIRECTORY_READ_RESULT_NOT_SUPPORTED ||
        !vfs_directory_test_is_sentinel(
            &result
        )
    ) {
        kernel_panic(
            "VFS directory missing operations rejection failed"
        );
    }

    struct vfs_file no_callback_file;

    if (!vfs_file_initialize(
        &no_callback_file,
        &directory,
        VFS_OPEN_ACCESS_READ,
        &vfs_directory_test_empty_file_operations,
        &stream
    )) {
        kernel_panic(
            "VFS directory no-callback fixture failed"
        );
    }

    vfs_directory_test_set_sentinel(
        &result
    );

    if (
        vfs_file_read_directory(
            &no_callback_file,
            &result
        ) !=
            VFS_DIRECTORY_READ_RESULT_NOT_SUPPORTED ||
        !vfs_directory_test_is_sentinel(
            &result
        )
    ) {
        kernel_panic(
            "VFS directory missing callback rejection failed"
        );
    }

    if (
        !vfs_file_release(
            &no_callback_file
        ) ||
        !vfs_file_release(
            &no_operations_file
        ) ||
        !vfs_file_release(
            &write_only_file
        ) ||
        !vfs_file_release(
            &regular_file
        ) ||
        !vfs_file_release(
            &readable_file
        ) ||
        !vfs_node_release(
            &regular
        ) ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "VFS directory validation fixture cleanup failed"
        );
    }
}

static void vfs_directory_test_backend_result_propagation(void)
{
    static const enum vfs_directory_read_result
        backend_results[] = {
            VFS_DIRECTORY_READ_RESULT_END,
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT,
            VFS_DIRECTORY_READ_RESULT_NOT_DIRECTORY,
            VFS_DIRECTORY_READ_RESULT_NOT_SUPPORTED,
            VFS_DIRECTORY_READ_RESULT_ACCESS_DENIED,
            VFS_DIRECTORY_READ_RESULT_INVALIDATED,
            VFS_DIRECTORY_READ_RESULT_RESOURCE_EXHAUSTED,
        };

    struct vfs_node directory;

    struct vfs_file file;

    struct vfs_directory_test_stream stream = {
        .mode =
            VFS_DIRECTORY_TEST_STREAM_FORCED_RESULT,
    };

    if (
        !vfs_node_initialize(
            &directory,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &file,
            &directory,
            VFS_OPEN_ACCESS_READ,
            &vfs_directory_test_file_operations,
            &stream
        )
    ) {
        kernel_panic(
            "VFS directory backend result fixture failed"
        );
    }

    for (
        size_t index = 0;
        index <
            sizeof(backend_results) /
            sizeof(backend_results[0]);
        ++index
    ) {
        stream.forced_result =
            backend_results[index];

        struct vfs_directory_entry result;

        vfs_directory_test_set_sentinel(
            &result
        );

        if (
            vfs_file_read_directory(
                &file,
                &result
            ) !=
                backend_results[index] ||
            !vfs_directory_test_is_sentinel(
                &result
            )
        ) {
            kernel_panic(
                "VFS directory backend result propagation failed"
            );
        }
    }

    if (
        !vfs_file_release(
            &file
        ) ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "VFS directory backend result cleanup failed"
        );
    }
}

static void vfs_directory_test_malformed_entries(void)
{
    struct vfs_node directory;

    struct vfs_file file;

    struct vfs_directory_test_stream stream = {
        .mode =
            VFS_DIRECTORY_TEST_STREAM_MALFORMED,
    };

    if (
        !vfs_node_initialize(
            &directory,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &file,
            &directory,
            VFS_OPEN_ACCESS_READ,
            &vfs_directory_test_file_operations,
            &stream
        )
    ) {
        kernel_panic(
            "VFS directory malformed-entry fixture failed"
        );
    }

    for (
        unsigned int kind =
            VFS_DIRECTORY_TEST_MALFORMED_EMPTY_NAME;
        kind <=
            VFS_DIRECTORY_TEST_MALFORMED_NODE_TYPE;
        ++kind
    ) {
        stream.malformed_kind =
            (enum vfs_directory_test_malformed_kind)
                kind;

        struct vfs_directory_entry result;

        vfs_directory_test_set_sentinel(
            &result
        );

        if (
            vfs_file_read_directory(
                &file,
                &result
            ) !=
                VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT ||
            !vfs_directory_test_is_sentinel(
                &result
            )
        ) {
            kernel_panic(
                "VFS directory malformed entry was published"
            );
        }
    }

    if (
        !vfs_file_release(
            &file
        ) ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "VFS directory malformed-entry cleanup failed"
        );
    }
}

static void vfs_directory_test_callback_violations(void)
{
    struct vfs_node directory;

    struct vfs_file file;

    struct vfs_directory_test_stream stream = {
        .mode =
            VFS_DIRECTORY_TEST_STREAM_FORCED_RESULT,
        .forced_result =
            (enum vfs_directory_read_result) 99,
    };

    if (
        !vfs_node_initialize(
            &directory,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &file,
            &directory,
            VFS_OPEN_ACCESS_READ,
            &vfs_directory_test_file_operations,
            &stream
        )
    ) {
        kernel_panic(
            "VFS directory callback violation fixture failed"
        );
    }

    struct vfs_directory_entry result;

    vfs_directory_test_set_sentinel(
        &result
    );

    if (
        vfs_file_read_directory(
            &file,
            &result
        ) !=
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT ||
        !vfs_directory_test_is_sentinel(
            &result
        )
    ) {
        kernel_panic(
            "VFS directory invalid backend result was accepted"
        );
    }

    stream.mode =
        VFS_DIRECTORY_TEST_STREAM_MUTATE_OFFSET;

    file.offset =
        0x1234U;

    vfs_directory_test_set_sentinel(
        &result
    );

    if (
        vfs_file_read_directory(
            &file,
            &result
        ) !=
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT ||
        file.offset !=
            0x1234U ||
        !vfs_directory_test_is_sentinel(
            &result
        )
    ) {
        kernel_panic(
            "VFS directory callback offset mutation was accepted"
        );
    }

    if (
        !vfs_file_release(
            &file
        ) ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "VFS directory callback violation cleanup failed"
        );
    }
}

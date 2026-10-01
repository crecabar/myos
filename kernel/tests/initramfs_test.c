// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file initramfs_test.c
 * @brief Initramfs VFS tree regression tests.
 */

#include "initramfs_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../fs/initramfs.h"
#include "../memory/heap.h"
#include "../runtime/memory.h"
#include "../vfs/path.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

// Defines
#define INITRAMFS_TEST_ARCHIVE_CAPACITY 4096U
#define INITRAMFS_TEST_HEADER_SIZE 110U
#define INITRAMFS_TEST_FIELD_SIZE 8U

#define INITRAMFS_TEST_MODE_DIRECTORY 0040755U
#define INITRAMFS_TEST_MODE_REGULAR 0100644U
#define INITRAMFS_TEST_MODE_SYMLINK 0120777U

// Private functions and helpers declarations
static void initramfs_test_write_hex(
    uint8_t *destination,
    uint32_t value
);

static size_t initramfs_test_align(
    size_t value
);

static size_t initramfs_test_append_entry(
    uint8_t *archive,
    size_t capacity,
    size_t offset,
    const char *name,
    size_t name_length,
    uint32_t mode,
    const uint8_t *data,
    size_t data_size
);

static size_t initramfs_test_append_trailer(
    uint8_t *archive,
    size_t capacity,
    size_t offset
);

static bool initramfs_test_heap_equal(
    const struct kernel_heap_stats *left,
    const struct kernel_heap_stats *right
);

static void initramfs_test_valid_tree(void);

static void initramfs_test_topology_errors(void);

static void initramfs_test_unsupported_archive(void);

static void initramfs_test_reference_lifetime(void);

static void initramfs_test_invalid_arguments(void);

// Public functions implementations
void initramfs_test_run(void)
{
    initramfs_test_valid_tree();
    initramfs_test_topology_errors();
    initramfs_test_unsupported_archive();
    initramfs_test_reference_lifetime();
    initramfs_test_invalid_arguments();

    diagnostics_write(
        "[fs] Initramfs VFS tree tests passed\n"
    );
}

// Private functions and helpers implementations
static void initramfs_test_write_hex(
    uint8_t *destination,
    uint32_t value)
{
    static const char digits[] =
        "0123456789abcdef";

    if (destination == NULL) {
        kernel_panic(
            "Initramfs tree test received null hex destination"
        );
    }

    for (
        size_t index = 0;
        index < INITRAMFS_TEST_FIELD_SIZE;
        ++index
    ) {
        size_t shift =
            (
                INITRAMFS_TEST_FIELD_SIZE -
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

static size_t initramfs_test_align(
    size_t value)
{
    return
        (value + 3U) &
        ~(size_t) 3U;
}

static size_t initramfs_test_append_entry(
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
            "Invalid initramfs tree test entry"
        );
    }

    if (
        data_size != 0 &&
        data == NULL
    ) {
        kernel_panic(
            "Initramfs tree test entry has null payload"
        );
    }

    size_t name_size =
        name_length + 1U;

    if (
        offset > capacity ||
        INITRAMFS_TEST_HEADER_SIZE >
            capacity - offset
    ) {
        kernel_panic(
            "Initramfs tree test header overflow"
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

    fields[0] =
        1U;

    fields[1] =
        mode;

    fields[4] =
        1U;

    fields[6] =
        (uint32_t) data_size;

    fields[11] =
        (uint32_t) name_size;

    for (
        size_t field = 0;
        field < 13U;
        ++field
    ) {
        initramfs_test_write_hex(
            header +
                6U +
                field *
                    INITRAMFS_TEST_FIELD_SIZE,
            fields[field]
        );
    }

    offset +=
        INITRAMFS_TEST_HEADER_SIZE;

    if (
        name_size >
        capacity - offset
    ) {
        kernel_panic(
            "Initramfs tree test name overflow"
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
        initramfs_test_align(
            offset
        );

    if (aligned > capacity) {
        kernel_panic(
            "Initramfs tree test name padding overflow"
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
            "Initramfs tree test payload overflow"
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
        initramfs_test_align(
            offset
        );

    if (aligned > capacity) {
        kernel_panic(
            "Initramfs tree test data padding overflow"
        );
    }

    while (offset < aligned) {
        archive[offset++] =
            0;
    }

    return
        offset;
}

static size_t initramfs_test_append_trailer(
    uint8_t *archive,
    size_t capacity,
    size_t offset)
{
    static const char trailer[] =
        "TRAILER!!!";

    return initramfs_test_append_entry(
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

static bool initramfs_test_heap_equal(
    const struct kernel_heap_stats *left,
    const struct kernel_heap_stats *right)
{
    if (
        left == NULL ||
        right == NULL
    ) {
        return false;
    }

    return
        left->page_count ==
            right->page_count &&
        left->allocated_block_count ==
            right->allocated_block_count &&
        left->allocated_bytes ==
            right->allocated_bytes;
}

static void initramfs_test_valid_tree(void)
{
    uint8_t archive[
        INITRAMFS_TEST_ARCHIVE_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    /*
     * Deliberately place a child before its parent. Mounting must not depend
     * on archive ordering.
     */
    static const uint8_t motd[] = {
        'M', 'y', 'O', 'S', '\n',
    };

    size_t size =
        0;

    size =
        initramfs_test_append_entry(
            archive,
            sizeof(archive),
            size,
            "etc/motd",
            sizeof("etc/motd") - 1U,
            INITRAMFS_TEST_MODE_REGULAR,
            motd,
            sizeof(motd)
        );

    size =
        initramfs_test_append_entry(
            archive,
            sizeof(archive),
            size,
            "bin",
            sizeof("bin") - 1U,
            INITRAMFS_TEST_MODE_DIRECTORY,
            NULL,
            0
        );

    size =
        initramfs_test_append_entry(
            archive,
            sizeof(archive),
            size,
            "etc",
            sizeof("etc") - 1U,
            INITRAMFS_TEST_MODE_DIRECTORY,
            NULL,
            0
        );

    size =
        initramfs_test_append_entry(
            archive,
            sizeof(archive),
            size,
            "bin/tool",
            sizeof("bin/tool") - 1U,
            INITRAMFS_TEST_MODE_REGULAR,
            NULL,
            0
        );

    size =
        initramfs_test_append_trailer(
            archive,
            sizeof(archive),
            size
        );

    struct kernel_heap_stats before;

    if (!kernel_heap_stats_get(
        &before
    )) {
        kernel_panic(
            "Unable to collect initramfs tree heap baseline"
        );
    }

    struct initramfs *filesystem =
        NULL;

    if (
        initramfs_mount(
            archive,
            size,
            &filesystem
        ) != INITRAMFS_MOUNT_RESULT_MOUNTED ||
        filesystem == NULL
    ) {
        kernel_panic(
            "Unable to mount valid initramfs tree"
        );
    }

    struct vfs_node *root =
        initramfs_root(
            filesystem
        );

    if (
        root == NULL ||
        root->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        root->reference_count != 1
    ) {
        kernel_panic(
            "Initramfs root contract mismatch"
        );
    }

    struct vfs_stat metadata = {
        .type =
            VFS_NODE_TYPE_CHARACTER_DEVICE,
        .size =
            UINT64_MAX,
    };

    if (
        vfs_node_stat(
            root,
            &metadata
        ) != VFS_STAT_RESULT_SUCCESS ||
        metadata.type !=
            VFS_NODE_TYPE_DIRECTORY ||
        metadata.size != 0
    ) {
        kernel_panic(
            "Initramfs root stat mismatch"
        );
    }

    struct vfs_node *motd_node =
        NULL;

    static const char motd_path[] =
        "/etc/motd";

    if (
        vfs_path_resolve(
            root,
            root,
            motd_path,
            sizeof(motd_path) - 1U,
            &motd_node
        ) != VFS_PATH_RESULT_FOUND ||
        motd_node == NULL ||
        motd_node->type !=
            VFS_NODE_TYPE_REGULAR_FILE
    ) {
        kernel_panic(
            "Initramfs nested file resolution failed"
        );
    }

    metadata.type =
        VFS_NODE_TYPE_CHARACTER_DEVICE;

    metadata.size =
        UINT64_MAX;

    if (
        vfs_node_stat(
            motd_node,
            &metadata
        ) != VFS_STAT_RESULT_SUCCESS ||
        metadata.type !=
            VFS_NODE_TYPE_REGULAR_FILE ||
        metadata.size !=
            sizeof(motd)
    ) {
        kernel_panic(
            "Initramfs regular-file stat mismatch"
        );
    }

    if (!vfs_node_release(
        motd_node
    )) {
        kernel_panic(
            "Initramfs nested file release failed"
        );
    }

    struct vfs_node *tool =
        NULL;

    static const char traversal[] =
        "/etc/../bin/tool";

    if (
        vfs_path_resolve(
            root,
            root,
            traversal,
            sizeof(traversal) - 1U,
            &tool
        ) != VFS_PATH_RESULT_FOUND ||
        tool == NULL ||
        tool->type !=
            VFS_NODE_TYPE_REGULAR_FILE
    ) {
        kernel_panic(
            "Initramfs parent traversal failed"
        );
    }

    if (!vfs_node_release(
        tool
    )) {
        kernel_panic(
            "Initramfs traversal result release failed"
        );
    }

    struct vfs_node *missing =
        NULL;

    if (
        vfs_node_lookup(
            root,
            "missing",
            sizeof("missing") - 1U,
            &missing
        ) != VFS_LOOKUP_RESULT_NOT_FOUND ||
        missing != NULL
    ) {
        kernel_panic(
            "Initramfs missing-child lookup mismatch"
        );
    }

    if (!initramfs_unmount(
        filesystem
    )) {
        kernel_panic(
            "Unable to unmount valid initramfs tree"
        );
    }

    struct kernel_heap_stats after;

    if (
        !kernel_heap_stats_get(
            &after
        ) ||
        !initramfs_test_heap_equal(
            &before,
            &after
        )
    ) {
        kernel_panic(
            "Initramfs tree mount/unmount leaked heap ownership"
        );
    }
}

static void initramfs_test_topology_errors(void)
{
    struct kernel_heap_stats baseline;

    if (!kernel_heap_stats_get(
        &baseline
    )) {
        kernel_panic(
            "Unable to collect initramfs topology heap baseline"
        );
    }

    /*
     * Duplicate pathname.
     */
    {
        uint8_t archive[
            INITRAMFS_TEST_ARCHIVE_CAPACITY
        ];

        memset(
            archive,
            0,
            sizeof(archive)
        );

        size_t size =
            0;

        size =
            initramfs_test_append_entry(
                archive,
                sizeof(archive),
                size,
                "dup",
                sizeof("dup") - 1U,
                INITRAMFS_TEST_MODE_REGULAR,
                NULL,
                0
            );

        size =
            initramfs_test_append_entry(
                archive,
                sizeof(archive),
                size,
                "dup",
                sizeof("dup") - 1U,
                INITRAMFS_TEST_MODE_REGULAR,
                NULL,
                0
            );

        size =
            initramfs_test_append_trailer(
                archive,
                sizeof(archive),
                size
            );

        struct initramfs *sentinel =
            (struct initramfs *)
            (uintptr_t) 1U;

        struct initramfs *result =
            sentinel;

        if (
            initramfs_mount(
                archive,
                size,
                &result
            ) !=
                INITRAMFS_MOUNT_RESULT_MALFORMED_ARCHIVE ||
            result != sentinel
        ) {
            kernel_panic(
                "Initramfs accepted duplicate pathname"
            );
        }
    }

    /*
     * Missing parent directory.
     */
    {
        uint8_t archive[
            INITRAMFS_TEST_ARCHIVE_CAPACITY
        ];

        memset(
            archive,
            0,
            sizeof(archive)
        );

        size_t size =
            initramfs_test_append_entry(
                archive,
                sizeof(archive),
                0,
                "missing/file",
                sizeof("missing/file") - 1U,
                INITRAMFS_TEST_MODE_REGULAR,
                NULL,
                0
            );

        size =
            initramfs_test_append_trailer(
                archive,
                sizeof(archive),
                size
            );

        struct initramfs *result =
            NULL;

        if (
            initramfs_mount(
                archive,
                size,
                &result
            ) !=
                INITRAMFS_MOUNT_RESULT_MALFORMED_ARCHIVE ||
            result != NULL
        ) {
            kernel_panic(
                "Initramfs accepted missing parent"
            );
        }
    }

    /*
     * A regular file cannot parent another node.
     */
    {
        uint8_t archive[
            INITRAMFS_TEST_ARCHIVE_CAPACITY
        ];

        memset(
            archive,
            0,
            sizeof(archive)
        );

        size_t size =
            0;

        size =
            initramfs_test_append_entry(
                archive,
                sizeof(archive),
                size,
                "file",
                sizeof("file") - 1U,
                INITRAMFS_TEST_MODE_REGULAR,
                NULL,
                0
            );

        size =
            initramfs_test_append_entry(
                archive,
                sizeof(archive),
                size,
                "file/child",
                sizeof("file/child") - 1U,
                INITRAMFS_TEST_MODE_REGULAR,
                NULL,
                0
            );

        size =
            initramfs_test_append_trailer(
                archive,
                sizeof(archive),
                size
            );

        struct initramfs *result =
            NULL;

        if (
            initramfs_mount(
                archive,
                size,
                &result
            ) !=
                INITRAMFS_MOUNT_RESULT_MALFORMED_ARCHIVE ||
            result != NULL
        ) {
            kernel_panic(
                "Initramfs accepted non-directory parent"
            );
        }
    }

    struct kernel_heap_stats after;

    if (
        !kernel_heap_stats_get(
            &after
        ) ||
        !initramfs_test_heap_equal(
            &baseline,
            &after
        )
    ) {
        kernel_panic(
            "Initramfs topology failure leaked heap ownership"
        );
    }
}

static void initramfs_test_unsupported_archive(void)
{
    uint8_t archive[
        INITRAMFS_TEST_ARCHIVE_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    size_t size =
        initramfs_test_append_entry(
            archive,
            sizeof(archive),
            0,
            "link",
            sizeof("link") - 1U,
            INITRAMFS_TEST_MODE_SYMLINK,
            NULL,
            0
        );

    size =
        initramfs_test_append_trailer(
            archive,
            sizeof(archive),
            size
        );

    struct initramfs *result =
        NULL;

    if (
        initramfs_mount(
            archive,
            size,
            &result
        ) !=
            INITRAMFS_MOUNT_RESULT_UNSUPPORTED_ARCHIVE ||
        result != NULL
    ) {
        kernel_panic(
            "Initramfs mount accepted unsupported object type"
        );
    }
}

static void initramfs_test_reference_lifetime(void)
{
    uint8_t archive[
        INITRAMFS_TEST_ARCHIVE_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    size_t size =
        initramfs_test_append_entry(
            archive,
            sizeof(archive),
            0,
            "etc",
            sizeof("etc") - 1U,
            INITRAMFS_TEST_MODE_DIRECTORY,
            NULL,
            0
        );

    size =
        initramfs_test_append_trailer(
            archive,
            sizeof(archive),
            size
        );

    struct initramfs *filesystem =
        NULL;

    if (
        initramfs_mount(
            archive,
            size,
            &filesystem
        ) != INITRAMFS_MOUNT_RESULT_MOUNTED ||
        filesystem == NULL
    ) {
        kernel_panic(
            "Unable to mount initramfs lifetime fixture"
        );
    }

    struct vfs_node *root =
        initramfs_root(
            filesystem
        );

    if (
        root == NULL ||
        !vfs_node_retain(
            root
        ) ||
        root->reference_count != 2
    ) {
        kernel_panic(
            "Unable to retain initramfs root reference"
        );
    }

    if (
        initramfs_unmount(
            filesystem
        ) ||
        root->reference_count != 2
    ) {
        kernel_panic(
            "Initramfs unmounted with outstanding root reference"
        );
    }

    if (
        !vfs_node_release(
            root
        ) ||
        root->reference_count != 1
    ) {
        kernel_panic(
            "Unable to release external initramfs root reference"
        );
    }

    struct vfs_node *etc =
        NULL;

    if (
        vfs_node_lookup(
            root,
            "etc",
            sizeof("etc") - 1U,
            &etc
        ) != VFS_LOOKUP_RESULT_FOUND ||
        etc == NULL ||
        etc->reference_count != 2
    ) {
        kernel_panic(
            "Unable to retain initramfs child reference"
        );
    }

    if (initramfs_unmount(
        filesystem
    )) {
        kernel_panic(
            "Initramfs unmounted with outstanding child reference"
        );
    }

    if (!vfs_node_release(
        etc
    )) {
        kernel_panic(
            "Unable to release external initramfs child reference"
        );
    }

    if (!initramfs_unmount(
        filesystem
    )) {
        kernel_panic(
            "Initramfs remained busy after reference release"
        );
    }
}

static void initramfs_test_invalid_arguments(void)
{
    uint8_t archive[
        INITRAMFS_TEST_ARCHIVE_CAPACITY
    ];

    memset(
        archive,
        0,
        sizeof(archive)
    );

    size_t size =
        initramfs_test_append_trailer(
            archive,
            sizeof(archive),
            0
        );

    struct initramfs *sentinel =
        (struct initramfs *)
        (uintptr_t) 1U;

    struct initramfs *result =
        sentinel;

    if (
        initramfs_mount(
            NULL,
            size,
            &result
        ) !=
            INITRAMFS_MOUNT_RESULT_INVALID_ARGUMENT ||
        result != sentinel ||
        initramfs_mount(
            archive,
            size,
            NULL
        ) !=
            INITRAMFS_MOUNT_RESULT_INVALID_ARGUMENT ||
        initramfs_root(
            NULL
        ) != NULL ||
        initramfs_unmount(
            NULL
        )
    ) {
        kernel_panic(
            "Initramfs accepted invalid public API arguments"
        );
    }
}

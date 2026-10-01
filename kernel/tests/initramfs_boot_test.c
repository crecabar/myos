// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file initramfs_boot_test.c
 * @brief Boot initramfs root filesystem integration regression.
 */

#include "initramfs_boot_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../runtime/memory.h"
#include "../vfs/path.h"
#include "../vfs/root.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static bool initramfs_boot_test_bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size
);

// Public functions implementations
void initramfs_boot_test_run(void)
{
    static const char path[] =
        "/init";

    static const uint8_t expected[] = {
        'M', 'y', 'O', 'S', ' ',
        'i', 'n', 'i', 't', 'r', 'a', 'm', 'f', 's', ' ',
        'p', 'l', 'a', 'c', 'e', 'h', 'o', 'l', 'd', 'e', 'r',
        '\n',
    };

    struct vfs_node *root =
        vfs_root_get();

    if (
        root == NULL ||
        root->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        root->reference_count != 2
    ) {
        kernel_panic(
            "Boot initramfs system root contract mismatch"
        );
    }

    struct vfs_node *init =
        NULL;

    if (
        vfs_path_resolve(
            root,
            root,
            path,
            sizeof(path) - 1U,
            &init
        ) != VFS_PATH_RESULT_FOUND ||
        init == NULL ||
        init->type !=
            VFS_NODE_TYPE_REGULAR_FILE
    ) {
        kernel_panic(
            "Boot initramfs /init resolution failed"
        );
    }

    struct vfs_stat metadata;

    if (
        vfs_node_stat(
            init,
            &metadata
        ) != VFS_STAT_RESULT_SUCCESS ||
        metadata.type !=
            VFS_NODE_TYPE_REGULAR_FILE ||
        metadata.size !=
            sizeof(expected)
    ) {
        kernel_panic(
            "Boot initramfs /init metadata mismatch"
        );
    }

    struct vfs_file *file =
        NULL;

    if (
        vfs_node_open(
            init,
            VFS_OPEN_ACCESS_READ,
            &file
        ) != VFS_OPEN_RESULT_OPENED ||
        file == NULL
    ) {
        kernel_panic(
            "Boot initramfs /init open failed"
        );
    }

    uint8_t buffer[
        sizeof(expected)
    ];

    memset(
        buffer,
        0,
        sizeof(buffer)
    );

    size_t bytes_read =
        SIZE_MAX;

    if (
        vfs_file_read(
            file,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) != VFS_IO_RESULT_SUCCESS ||
        bytes_read !=
            sizeof(expected) ||
        !initramfs_boot_test_bytes_equal(
            buffer,
            expected,
            sizeof(expected)
        )
    ) {
        kernel_panic(
            "Boot initramfs /init contents mismatch"
        );
    }

    bytes_read =
        SIZE_MAX;

    if (
        vfs_file_read(
            file,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) != VFS_IO_RESULT_SUCCESS ||
        bytes_read != 0 ||
        file->offset !=
            sizeof(expected)
    ) {
        kernel_panic(
            "Boot initramfs /init EOF mismatch"
        );
    }

    if (!vfs_file_release(
        file
    )) {
        kernel_panic(
            "Boot initramfs /init file release failed"
        );
    }

    if (!vfs_node_release(
        init
    )) {
        kernel_panic(
            "Boot initramfs /init node release failed"
        );
    }

    /*
     * Only the initramfs filesystem and the global VFS root publication
     * should own the root after temporary traversal references are gone.
     */
    if (
        root->reference_count !=
        2
    ) {
        kernel_panic(
            "Boot initramfs root reference leaked"
        );
    }

    diagnostics_write(
        "[fs] Boot initramfs root integration test passed\n"
    );
}

// Private functions and helpers implementations
static bool initramfs_boot_test_bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size)
{
    if (
        left == NULL ||
        right == NULL
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < size;
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

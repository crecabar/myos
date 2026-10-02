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
static void initramfs_boot_test_core_devices(
    struct vfs_node *root
);

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

    initramfs_boot_test_core_devices(
        root
    );

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
static void initramfs_boot_test_core_devices(
    struct vfs_node *root)
{
    if (root == NULL) {
        kernel_panic(
            "Boot core-device test received null root"
        );
    }

    static const char dev_path[] =
        "/dev";

    struct vfs_node *dev =
        NULL;

    if (
        vfs_path_resolve(
            root,
            root,
            dev_path,
            sizeof(dev_path) - 1U,
            &dev
        ) !=
            VFS_PATH_RESULT_FOUND ||
        dev == NULL ||
        dev->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        kernel_panic(
            "Boot /dev directory resolution failed"
        );
    }

    if (!vfs_node_release(
        dev
    )) {
        kernel_panic(
            "Boot /dev directory release failed"
        );
    }

    /*
     * /dev/null: reads are immediate EOF and writes consume every supplied
     * byte through the ordinary VFS character-device path.
     */
    static const char null_path[] =
        "/dev/null";

    struct vfs_node *null_node =
        NULL;

    if (
        vfs_path_resolve(
            root,
            root,
            null_path,
            sizeof(null_path) - 1U,
            &null_node
        ) !=
            VFS_PATH_RESULT_FOUND ||
        null_node == NULL ||
        null_node->type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE
    ) {
        kernel_panic(
            "Boot /dev/null resolution failed"
        );
    }

    struct vfs_stat metadata = {
        .type =
            VFS_NODE_TYPE_REGULAR_FILE,
        .size =
            UINT64_MAX,
    };

    if (
        vfs_node_stat(
            null_node,
            &metadata
        ) !=
            VFS_STAT_RESULT_SUCCESS ||
        metadata.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        metadata.size != 0
    ) {
        kernel_panic(
            "Boot /dev/null metadata mismatch"
        );
    }

    struct vfs_file *null_file =
        NULL;

    if (
        vfs_node_open(
            null_node,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &null_file
        ) !=
            VFS_OPEN_RESULT_OPENED ||
        null_file == NULL
    ) {
        kernel_panic(
            "Boot /dev/null open failed"
        );
    }

    uint8_t null_buffer[4] = {
        0x11U,
        0x22U,
        0x33U,
        0x44U,
    };

    size_t transferred =
        SIZE_MAX;

    if (
        vfs_file_read(
            null_file,
            null_buffer,
            sizeof(null_buffer),
            &transferred
        ) !=
            VFS_IO_RESULT_SUCCESS ||
        transferred != 0 ||
        null_buffer[0] != 0x11U ||
        null_buffer[1] != 0x22U ||
        null_buffer[2] != 0x33U ||
        null_buffer[3] != 0x44U
    ) {
        kernel_panic(
            "Boot /dev/null read semantics failed"
        );
    }

    transferred =
        SIZE_MAX;

    if (
        vfs_file_write(
            null_file,
            null_buffer,
            sizeof(null_buffer),
            &transferred
        ) !=
            VFS_IO_RESULT_SUCCESS ||
        transferred !=
            sizeof(null_buffer)
    ) {
        kernel_panic(
            "Boot /dev/null write semantics failed"
        );
    }

    uint64_t seek_result =
        UINT64_MAX;

    if (
        vfs_file_seek(
            null_file,
            0,
            VFS_SEEK_ORIGIN_START,
            &seek_result
        ) !=
            VFS_SEEK_RESULT_NOT_SUPPORTED ||
        seek_result !=
            UINT64_MAX
    ) {
        kernel_panic(
            "Boot /dev/null unexpectedly supported seek"
        );
    }

    if (
        !vfs_file_release(
            null_file
        ) ||
        !vfs_node_release(
            null_node
        )
    ) {
        kernel_panic(
            "Boot /dev/null ownership cleanup failed"
        );
    }

    /*
     * /dev/zero: reads fill the complete requested range with zero and writes
     * consume every byte.
     */
    static const char zero_path[] =
        "/dev/zero";

    struct vfs_node *zero_node =
        NULL;

    if (
        vfs_path_resolve(
            root,
            root,
            zero_path,
            sizeof(zero_path) - 1U,
            &zero_node
        ) !=
            VFS_PATH_RESULT_FOUND ||
        zero_node == NULL ||
        zero_node->type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE
    ) {
        kernel_panic(
            "Boot /dev/zero resolution failed"
        );
    }

    metadata.type =
        VFS_NODE_TYPE_REGULAR_FILE;

    metadata.size =
        UINT64_MAX;

    if (
        vfs_node_stat(
            zero_node,
            &metadata
        ) !=
            VFS_STAT_RESULT_SUCCESS ||
        metadata.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        metadata.size != 0
    ) {
        kernel_panic(
            "Boot /dev/zero metadata mismatch"
        );
    }

    struct vfs_file *zero_file =
        NULL;

    if (
        vfs_node_open(
            zero_node,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &zero_file
        ) !=
            VFS_OPEN_RESULT_OPENED ||
        zero_file == NULL
    ) {
        kernel_panic(
            "Boot /dev/zero open failed"
        );
    }

    uint8_t zero_buffer[8] = {
        0xa1U,
        0xa2U,
        0xa3U,
        0xa4U,
        0xb1U,
        0xb2U,
        0xb3U,
        0xb4U,
    };

    transferred =
        SIZE_MAX;

    if (
        vfs_file_read(
            zero_file,
            zero_buffer,
            sizeof(zero_buffer),
            &transferred
        ) !=
            VFS_IO_RESULT_SUCCESS ||
        transferred !=
            sizeof(zero_buffer)
    ) {
        kernel_panic(
            "Boot /dev/zero read failed"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(zero_buffer);
        ++index
    ) {
        if (zero_buffer[index] != 0) {
            kernel_panic(
                "Boot /dev/zero returned non-zero data"
            );
        }
    }

    transferred =
        SIZE_MAX;

    if (
        vfs_file_write(
            zero_file,
            zero_buffer,
            sizeof(zero_buffer),
            &transferred
        ) !=
            VFS_IO_RESULT_SUCCESS ||
        transferred !=
            sizeof(zero_buffer)
    ) {
        kernel_panic(
            "Boot /dev/zero write semantics failed"
        );
    }

    if (
        !vfs_file_release(
            zero_file
        ) ||
        !vfs_node_release(
            zero_node
        )
    ) {
        kernel_panic(
            "Boot /dev/zero ownership cleanup failed"
        );
    }

    /*
     * /dev/console exposes the visible kernel console as a write-only
     * character device. Input is deliberately unsupported until console input
     * receives blocking and terminal semantics of its own.
     */
    static const char console_path[] =
        "/dev/console";

    struct vfs_node *console_node =
        NULL;

    if (
        vfs_path_resolve(
            root,
            root,
            console_path,
            sizeof(console_path) - 1U,
            &console_node
        ) !=
            VFS_PATH_RESULT_FOUND ||
        console_node == NULL ||
        console_node->type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE
    ) {
        kernel_panic(
            "Boot /dev/console resolution failed"
        );
    }

    metadata.type =
        VFS_NODE_TYPE_REGULAR_FILE;

    metadata.size =
        UINT64_MAX;

    if (
        vfs_node_stat(
            console_node,
            &metadata
        ) !=
            VFS_STAT_RESULT_SUCCESS ||
        metadata.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        metadata.size != 0
    ) {
        kernel_panic(
            "Boot /dev/console metadata mismatch"
        );
    }

    struct vfs_file *rejected_file =
        NULL;

    if (
        vfs_node_open(
            console_node,
            VFS_OPEN_ACCESS_READ,
            &rejected_file
        ) !=
            VFS_OPEN_RESULT_NOT_SUPPORTED ||
        rejected_file != NULL
    ) {
        kernel_panic(
            "Boot /dev/console unexpectedly supported read open"
        );
    }

    rejected_file =
        NULL;

    if (
        vfs_node_open(
            console_node,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &rejected_file
        ) !=
            VFS_OPEN_RESULT_NOT_SUPPORTED ||
        rejected_file != NULL
    ) {
        kernel_panic(
            "Boot /dev/console unexpectedly supported read-write open"
        );
    }

    struct vfs_file *console_file =
        NULL;

    if (
        vfs_node_open(
            console_node,
            VFS_OPEN_ACCESS_WRITE,
            &console_file
        ) !=
            VFS_OPEN_RESULT_OPENED ||
        console_file == NULL
    ) {
        kernel_panic(
            "Boot /dev/console write open failed"
        );
    }

    /*
     * Use one embedded NUL byte so the complete VFS -> character-device ->
     * console backend path is exercised without printing diagnostic-looking
     * text into the visible test console.
     *
     * The console device is length-delimited; NUL is therefore a real byte,
     * not a string terminator.
     */
    const uint8_t console_byte =
        0;

    transferred =
        SIZE_MAX;

    if (
        vfs_file_write(
            console_file,
            &console_byte,
            sizeof(console_byte),
            &transferred
        ) !=
            VFS_IO_RESULT_SUCCESS ||
        transferred !=
            sizeof(console_byte) ||
        console_file->offset !=
            sizeof(console_byte)
    ) {
        kernel_panic(
            "Boot /dev/console write semantics failed"
        );
    }

    seek_result =
        UINT64_MAX;

    if (
        vfs_file_seek(
            console_file,
            0,
            VFS_SEEK_ORIGIN_START,
            &seek_result
        ) !=
            VFS_SEEK_RESULT_NOT_SUPPORTED ||
        seek_result !=
            UINT64_MAX
    ) {
        kernel_panic(
            "Boot /dev/console unexpectedly supported seek"
        );
    }

    if (
        !vfs_file_release(
            console_file
        ) ||
        !vfs_node_release(
            console_node
        )
    ) {
        kernel_panic(
            "Boot /dev/console ownership cleanup failed"
        );
    }
}

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

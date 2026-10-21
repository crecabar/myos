// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file core_device_fd_test.c
 * @brief Core device-node process/descriptor integration regression.
 *
 * This test deliberately knows nothing about the device registry or concrete
 * device implementations. It exercises the installed system namespace exactly
 * through process pathname and file-descriptor operations.
 */

#include "core_device_fd_test.h"

#include <myos/kernel/core/panic.h>
#include <myos/kernel/diagnostics/diagnostics.h>
#include <myos/kernel/process/fd_table.h>
#include <myos/kernel/process/file.h>
#include <myos/kernel/process/namespace.h>
#include <myos/kernel/vfs/root.h>
#include <myos/kernel/vfs/vfs.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static void core_device_fd_test_null(
    struct process_instance *instance
);

static void core_device_fd_test_zero(
    struct process_instance *instance
);

static void core_device_fd_test_console(
    struct process_instance *instance
);

// Public functions implementations
void core_device_fd_test_run(
    const struct vfs_mount_table *mounts)
{
    if (mounts == NULL) {
        kernel_panic(
            "Core device FD test has no mount topology"
        );
    }

    struct vfs_node *root =
        vfs_root_get();

    if (
        root == NULL ||
        root->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        kernel_panic(
            "Core device FD test has no system root"
        );
    }

    size_t root_references_before =
        root->reference_count;

    struct process_instance instance = {0};

    if (
        !process_fd_table_initialize(
            &instance.file_descriptors
        ) ||
        !process_namespace_root_set(
            &instance,
            root,
            mounts
        )
    ) {
        kernel_panic(
            "Unable to initialize core device FD process fixture"
        );
    }

    if (
        root->reference_count !=
            root_references_before + 1U ||
        process_namespace_mounts_get(
            &instance
        ) != mounts
    ) {
        kernel_panic(
            "Core device FD fixture did not install namespace"
        );
    }

    core_device_fd_test_null(
        &instance
    );

    core_device_fd_test_zero(
        &instance
    );

    core_device_fd_test_console(
        &instance
    );

    if (
        !process_fd_table_release_all(
            &instance.file_descriptors
        ) ||
        !process_namespace_root_release(
            &instance
        ) ||
        process_namespace_mounts_get(
            &instance
        ) != NULL ||
        root->reference_count !=
            root_references_before
    ) {
        kernel_panic(
            "Core device FD fixture leaked ownership"
        );
    }

    diagnostics_write(
        "[process] Core device VFS/FD integration test passed\n"
    );
}

// Private functions and helpers implementations
static void core_device_fd_test_null(
    struct process_instance *instance)
{
    static const char path[] =
        "/dev/null";

    size_t descriptor =
        SIZE_MAX;

    if (
        process_file_open(
            instance,
            path,
            sizeof(path) - 1U,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &descriptor
        ) !=
            PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0
    ) {
        kernel_panic(
            "Unable to open /dev/null through process FD path"
        );
    }

    bool ready = true;

    if (
        process_file_read_ready(
            instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        !ready
    ) {
        kernel_panic(
            "/dev/null process readiness contract failed"
        );
    }

    uint8_t buffer[4] = {
        0x11U,
        0x22U,
        0x33U,
        0x44U,
    };

    size_t transferred =
        SIZE_MAX;

    if (
        process_file_read(
            instance,
            descriptor,
            buffer,
            sizeof(buffer),
            &transferred
        ) !=
            PROCESS_FILE_RESULT_SUCCESS ||
        transferred != 0 ||
        buffer[0] != 0x11U ||
        buffer[1] != 0x22U ||
        buffer[2] != 0x33U ||
        buffer[3] != 0x44U
    ) {
        kernel_panic(
            "/dev/null process FD read semantics failed"
        );
    }

    transferred =
        SIZE_MAX;

    if (
        process_file_write(
            instance,
            descriptor,
            buffer,
            sizeof(buffer),
            &transferred
        ) !=
            PROCESS_FILE_RESULT_SUCCESS ||
        transferred !=
            sizeof(buffer)
    ) {
        kernel_panic(
            "/dev/null process FD write semantics failed"
        );
    }

    struct vfs_stat metadata = {
        .type =
            VFS_NODE_TYPE_REGULAR_FILE,
        .size =
            UINT64_MAX,
    };

    if (
        process_file_stat(
            instance,
            descriptor,
            &metadata
        ) !=
            PROCESS_FILE_RESULT_SUCCESS ||
        metadata.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        metadata.size != 0
    ) {
        kernel_panic(
            "/dev/null process FD stat failed"
        );
    }

    uint64_t seek_result =
        UINT64_MAX;

    if (
        process_file_seek(
            instance,
            descriptor,
            0,
            VFS_SEEK_ORIGIN_START,
            &seek_result
        ) !=
            PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        seek_result !=
            UINT64_MAX
    ) {
        kernel_panic(
            "/dev/null process FD unexpectedly supported seek"
        );
    }

    if (
        process_file_close(
            instance,
            descriptor
        ) !=
            PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to close /dev/null process descriptor"
        );
    }
}

static void core_device_fd_test_zero(
    struct process_instance *instance)
{
    static const char path[] =
        "/dev/zero";

    size_t descriptor =
        SIZE_MAX;

    if (
        process_file_open(
            instance,
            path,
            sizeof(path) - 1U,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &descriptor
        ) !=
            PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0
    ) {
        kernel_panic(
            "Unable to open /dev/zero through process FD path"
        );
    }

    bool ready = true;

    if (
        process_file_read_ready(
            instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        !ready
    ) {
        kernel_panic(
            "/dev/zero process readiness contract failed"
        );
    }

    uint8_t buffer[8] = {
        0xa1U,
        0xa2U,
        0xa3U,
        0xa4U,
        0xb1U,
        0xb2U,
        0xb3U,
        0xb4U,
    };

    size_t transferred =
        SIZE_MAX;

    if (
        process_file_read(
            instance,
            descriptor,
            buffer,
            sizeof(buffer),
            &transferred
        ) !=
            PROCESS_FILE_RESULT_SUCCESS ||
        transferred !=
            sizeof(buffer)
    ) {
        kernel_panic(
            "/dev/zero process FD read failed"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(buffer);
        ++index
    ) {
        if (buffer[index] != 0) {
            kernel_panic(
                "/dev/zero process FD returned non-zero data"
            );
        }
    }

    transferred =
        SIZE_MAX;

    if (
        process_file_write(
            instance,
            descriptor,
            buffer,
            sizeof(buffer),
            &transferred
        ) !=
            PROCESS_FILE_RESULT_SUCCESS ||
        transferred !=
            sizeof(buffer)
    ) {
        kernel_panic(
            "/dev/zero process FD write semantics failed"
        );
    }

    if (
        process_file_close(
            instance,
            descriptor
        ) !=
            PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to close /dev/zero process descriptor"
        );
    }
}

static void core_device_fd_test_console(
    struct process_instance *instance)
{
    static const char path[] =
        "/dev/console";

    /*
     * Console input is now readable. An empty line queue
     * reports WOULD_BLOCK rather than EOF.
     */
    size_t read_descriptor = SIZE_MAX;

    if (
        process_file_open(
            instance,
            path,
            sizeof(path) - 1U,
            VFS_OPEN_ACCESS_READ,
            &read_descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        read_descriptor != 0
    ) {
        kernel_panic(
            "/dev/console process FD read open failed"
        );
    }

    bool ready = true;

    if (
        process_file_read_ready(
            instance,
            read_descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        ready
    ) {
        kernel_panic(
            "/dev/console empty readiness contract failed"
        );
    }

    uint8_t input_byte = 0xa5U;
    size_t bytes_read = SIZE_MAX;

    if (
        process_file_read(
            instance,
            read_descriptor,
            &input_byte,
            sizeof(input_byte),
            &bytes_read
        ) != PROCESS_FILE_RESULT_WOULD_BLOCK ||
        input_byte != 0xa5U ||
        bytes_read != SIZE_MAX
    ) {
        kernel_panic(
            "/dev/console process FD empty input contract failed"
        );
    }

    if (
        process_file_close(
            instance,
            read_descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "/dev/console process FD read close failed"
        );
    }

    size_t descriptor =
        SIZE_MAX;

    if (
        process_file_open(
            instance,
            path,
            sizeof(path) - 1U,
            VFS_OPEN_ACCESS_WRITE,
            &descriptor
        ) !=
            PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0
    ) {
        kernel_panic(
            "Unable to open /dev/console through process FD path"
        );
    }

    ready = true;

    if (
        process_file_read_ready(
            instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_ACCESS_DENIED ||
        !ready
    ) {
        kernel_panic(
            "/dev/console write-only readiness contract failed"
        );
    }

    /*
     * Exercise the complete descriptor write path without adding
     * diagnostic-looking text to the test output.
     */
    const uint8_t byte =
        0;

    size_t transferred =
        SIZE_MAX;

    if (
        process_file_write(
            instance,
            descriptor,
            &byte,
            sizeof(byte),
            &transferred
        ) !=
            PROCESS_FILE_RESULT_SUCCESS ||
        transferred !=
            sizeof(byte)
    ) {
        kernel_panic(
            "/dev/console process FD write failed"
        );
    }

    struct vfs_stat metadata = {
        .type =
            VFS_NODE_TYPE_REGULAR_FILE,
        .size =
            UINT64_MAX,
    };

    if (
        process_file_stat(
            instance,
            descriptor,
            &metadata
        ) !=
            PROCESS_FILE_RESULT_SUCCESS ||
        metadata.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        metadata.size != 0
    ) {
        kernel_panic(
            "/dev/console process FD stat failed"
        );
    }

    uint64_t seek_result =
        UINT64_MAX;

    if (
        process_file_seek(
            instance,
            descriptor,
            0,
            VFS_SEEK_ORIGIN_START,
            &seek_result
        ) !=
            PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        seek_result !=
            UINT64_MAX
    ) {
        kernel_panic(
            "/dev/console process FD unexpectedly supported seek"
        );
    }

    if (
        process_file_close(
            instance,
            descriptor
        ) !=
            PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to close /dev/console process descriptor"
        );
    }
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file executable.c
 * @brief VFS-backed executable image acquisition implementation.
 */

#include "executable.h"

#include "cwd.h"
#include "namespace.h"

#include "../vfs/path.h"
#include "../core/panic.h"

#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static bool process_executable_source_read(
    const void *context,
    uint64_t offset,
    void *buffer,
    size_t size
);

static enum process_executable_result
process_executable_open_resolved_elf64(
    struct vfs_node *node,
    struct process_executable *executable
);

static enum process_executable_result
process_executable_map_vfs_path_result(
    enum vfs_path_result result
);

static enum process_executable_result
process_executable_open_elf64_from_context(
    struct vfs_node *root,
    struct vfs_node *start,
    const struct vfs_mount_table *mounts,
    const char *path,
    size_t path_length,
    struct process_executable *executable
);

static enum process_executable_result
process_executable_map_open_result(
    enum vfs_open_result result
);

static enum process_executable_result
process_executable_map_stat_result(
    enum vfs_stat_result result
);

// Public functions implementations
enum process_executable_result process_executable_open_elf64(
    const struct process_instance *instance,
    const char *path,
    size_t path_length,
    struct process_executable *executable)
{
    if (
        instance == NULL ||
        executable == NULL ||
        executable->file != NULL ||
        executable->image.source_read != NULL ||
        path == NULL ||
        path_length == 0 ||
        path_length > VFS_PATH_MAX
    ) {
        return
            PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_node *root =
        process_namespace_root_get(
            instance
        );

    if (root == NULL) {
        return
            PROCESS_EXECUTABLE_RESULT_NO_NAMESPACE_ROOT;
    }

    struct vfs_node *start =
        root;

    if (path[0] != '/') {
        start =
            process_cwd_get(
                instance
            );

        if (start == NULL) {
            return
                PROCESS_EXECUTABLE_RESULT_NO_CURRENT_DIRECTORY;
        }
    }

    return
        process_executable_open_elf64_from_context(
            root,
            start,
            process_namespace_mounts_get(
                instance
            ),
            path,
            path_length,
            executable
        );
}

enum process_executable_result
process_executable_open_elf64_from_namespace(
    struct vfs_node *root,
    const struct vfs_mount_table *mounts,
    const char *path,
    size_t path_length,
    struct process_executable *executable)
{
    if (root == NULL) {
        return
            PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT;
    }

    return
        process_executable_open_elf64_from_context(
            root,
            root,
            mounts,
            path,
            path_length,
            executable
        );
}

bool process_executable_close(
    struct process_executable *executable)
{
    if (
        executable == NULL ||
        executable->file == NULL ||
        executable->image.source_read == NULL
    ) {
        return false;
    }

    if (!vfs_file_release(
        executable->file
    )) {
        return false;
    }

    executable->file =
        NULL;

    executable->image =
        (struct elf64_image) {0};

    return true;
}

// Private functions and helpers implementations
static bool process_executable_source_read(
    const void *context,
    uint64_t offset,
    void *buffer,
    size_t size)
{
    if (context == NULL) {
        return false;
    }

    if (
        buffer == NULL &&
        size != 0
    ) {
        return false;
    }

    /*
     * context points at process_executable.file. The pointer slot itself is
     * borrowed read-only by the ELF source, while the referenced vfs_file
     * remains a live mutable kernel object.
     */
    struct vfs_file *const *file_storage =
        (struct vfs_file *const *) context;

    struct vfs_file *file =
        *file_storage;

    if (file == NULL) {
        return false;
    }

    uint8_t *destination =
        (uint8_t *) buffer;

    size_t transferred_total =
        0;

    while (transferred_total < size) {
        if (
            (uint64_t) transferred_total >
            UINT64_MAX - offset
        ) {
            return false;
        }

        size_t transferred =
            0;

        enum vfs_io_result result =
            vfs_file_read_at(
                file,
                offset +
                    (uint64_t)
                    transferred_total,
                destination +
                    transferred_total,
                size -
                    transferred_total,
                &transferred
            );

        if (
            result !=
                VFS_IO_RESULT_SUCCESS ||
            transferred == 0
        ) {
            return false;
        }

        transferred_total +=
            transferred;
    }

    return true;
}

static enum process_executable_result
process_executable_open_elf64_from_context(
    struct vfs_node *root,
    struct vfs_node *start,
    const struct vfs_mount_table *mounts,
    const char *path,
    size_t path_length,
    struct process_executable *executable)
{
    if (
        root == NULL ||
        start == NULL ||
        path == NULL ||
        path_length == 0 ||
        path_length > VFS_PATH_MAX ||
        executable == NULL ||
        executable->file != NULL ||
        executable->image.source_read != NULL
    ) {
        return
            PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_node *node =
        NULL;

    enum vfs_path_result path_result =
        vfs_path_resolve(
            root,
            start,
            mounts,
            path,
            path_length,
            &node
        );

    if (
        path_result !=
        VFS_PATH_RESULT_FOUND
    ) {
        return
            process_executable_map_vfs_path_result(
                path_result
            );
    }

    return
        process_executable_open_resolved_elf64(
            node,
            executable
        );
}

static enum process_executable_result
process_executable_open_resolved_elf64(
    struct vfs_node *node,
    struct process_executable *executable)
{
    if (
        node == NULL ||
        executable == NULL ||
        executable->file != NULL ||
        executable->image.source_read != NULL
    ) {
        if (node != NULL) {
            (void) vfs_node_release(
                node
            );
        }

        return
            PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_stat metadata;

    enum vfs_stat_result stat_result =
        vfs_node_stat(
            node,
            &metadata
        );

    if (
        stat_result !=
        VFS_STAT_RESULT_SUCCESS
    ) {
        if (!vfs_node_release(
            node
        )) {
            kernel_panic(
                "Executable open failed to release resolved node"
            );
        }

        return
            process_executable_map_stat_result(
                stat_result
            );
    }

    if (
        metadata.type !=
        VFS_NODE_TYPE_REGULAR_FILE
    ) {
        if (!vfs_node_release(
            node
        )) {
            kernel_panic(
                "Executable type rejection failed to release node"
            );
        }

        return
            PROCESS_EXECUTABLE_RESULT_NOT_REGULAR_FILE;
    }

    /*
     * elf64_source currently expresses its source size using size_t while VFS
     * metadata uses uint64_t. Reject any value that cannot round-trip through
     * the native source-size representation.
     */
    size_t source_size =
        (size_t) metadata.size;

    if (
        (uint64_t) source_size !=
        metadata.size
    ) {
        if (!vfs_node_release(
            node
        )) {
            kernel_panic(
                "Executable size rejection failed to release node"
            );
        }

        return
            PROCESS_EXECUTABLE_RESULT_OVERFLOW;
    }

    struct vfs_file *file =
        NULL;

    enum vfs_open_result open_result =
        vfs_node_open(
            node,
            VFS_OPEN_ACCESS_READ,
            &file
        );

    /*
     * An opened file owns an independent node reference. The pathname
     * resolver's reference is no longer needed regardless of open result.
     */
    if (!vfs_node_release(
        node
    )) {
        kernel_panic(
            "Executable open failed to release resolver node"
        );
    }

    if (
        open_result !=
        VFS_OPEN_RESULT_OPENED
    ) {
        return
            process_executable_map_open_result(
                open_result
            );
    }

    executable->file =
        file;

    struct elf64_source source = {
        .context =
            &executable->file,
        .size =
            source_size,
        .read =
            process_executable_source_read,
    };

    if (!elf64_parse_source(
        &source,
        &executable->image
    )) {
        if (!vfs_file_release(
            executable->file
        )) {
            kernel_panic(
                "Invalid executable rollback failed to release file"
            );
        }

        executable->file =
            NULL;

        executable->image =
            (struct elf64_image) {0};

        return
            PROCESS_EXECUTABLE_RESULT_INVALID_IMAGE;
    }

    return
        PROCESS_EXECUTABLE_RESULT_SUCCESS;
}

static enum process_executable_result
process_executable_map_vfs_path_result(
    enum vfs_path_result result)
{
    switch (result) {
        case VFS_PATH_RESULT_FOUND:
            return
                PROCESS_EXECUTABLE_RESULT_SUCCESS;

        case VFS_PATH_RESULT_NOT_FOUND:
            return
                PROCESS_EXECUTABLE_RESULT_NOT_FOUND;

        case VFS_PATH_RESULT_INVALID_ARGUMENT:
            return
                PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT;

        case VFS_PATH_RESULT_NOT_DIRECTORY:
            return
                PROCESS_EXECUTABLE_RESULT_NOT_DIRECTORY;

        case VFS_PATH_RESULT_NOT_SUPPORTED:
            return
                PROCESS_EXECUTABLE_RESULT_NOT_SUPPORTED;

        case VFS_PATH_RESULT_RESOURCE_EXHAUSTED:
            return
                PROCESS_EXECUTABLE_RESULT_RESOURCE_EXHAUSTED;
    }

    return
        PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT;
}

static enum process_executable_result
process_executable_map_open_result(
    enum vfs_open_result result)
{
    switch (result) {
        case VFS_OPEN_RESULT_OPENED:
            return
                PROCESS_EXECUTABLE_RESULT_SUCCESS;

        case VFS_OPEN_RESULT_INVALID_ARGUMENT:
            return
                PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT;

        case VFS_OPEN_RESULT_NOT_SUPPORTED:
            return
                PROCESS_EXECUTABLE_RESULT_NOT_SUPPORTED;

        case VFS_OPEN_RESULT_ACCESS_DENIED:
            return
                PROCESS_EXECUTABLE_RESULT_ACCESS_DENIED;

        case VFS_OPEN_RESULT_RESOURCE_EXHAUSTED:
            return
                PROCESS_EXECUTABLE_RESULT_RESOURCE_EXHAUSTED;
    }

    return
        PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT;
}

static enum process_executable_result
process_executable_map_stat_result(
    enum vfs_stat_result result)
{
    switch (result) {
        case VFS_STAT_RESULT_SUCCESS:
            return
                PROCESS_EXECUTABLE_RESULT_SUCCESS;

        case VFS_STAT_RESULT_INVALID_ARGUMENT:
            return
                PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT;

        case VFS_STAT_RESULT_NOT_SUPPORTED:
            return
                PROCESS_EXECUTABLE_RESULT_NOT_SUPPORTED;
    }

    return
        PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT;
}

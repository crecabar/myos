// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file initramfs.c
 * @brief Read-only initramfs VFS tree implementation.
 */

#include "initramfs.h"

#include "initramfs_format.h"

#include "../core/panic.h"
#include "../memory/heap.h"
#include "../runtime/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct initramfs_node {
    struct vfs_node vfs;

    struct initramfs_node *parent;

    struct initramfs_node *first_child;
    struct initramfs_node *next_sibling;

    struct initramfs_node *next_all;

    const char *path;
    size_t path_length;

    const char *name;
    size_t name_length;

    const uint8_t *data;
    size_t size;
};

struct initramfs_file {
    struct vfs_file vfs;
};

struct initramfs {
    const uint8_t *archive;
    size_t archive_size;

    struct initramfs_node root;

    struct initramfs_node *nodes;
    size_t node_count;
};

// Private functions and helpers declarations
static enum initramfs_mount_result initramfs_format_result_map(
    enum initramfs_format_result result
);

static enum vfs_node_type initramfs_vfs_type(
    enum initramfs_format_entry_type type
);

static bool initramfs_path_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
);

static void initramfs_path_basename(
    const char *path,
    size_t path_length,
    const char **name,
    size_t *name_length
);

static bool initramfs_path_parent_length(
    const char *path,
    size_t path_length,
    size_t *parent_length
);

static struct initramfs_node *initramfs_find_path(
    const struct initramfs *filesystem,
    const char *path,
    size_t path_length
);

static enum initramfs_mount_result initramfs_materialize_entries(
    struct initramfs *filesystem
);

static enum initramfs_mount_result initramfs_link_tree(
    struct initramfs *filesystem
);

static bool initramfs_has_external_references(
    const struct initramfs *filesystem
);

static void initramfs_destroy(
    struct initramfs *filesystem
);

static struct initramfs_node *initramfs_node_from_vfs(
    struct vfs_node *node
);

static enum vfs_open_result initramfs_node_open(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result
);

static enum vfs_io_result initramfs_file_read(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

static void initramfs_file_destroy(
    struct vfs_file *file
);

static enum vfs_stat_result initramfs_node_stat(
    struct vfs_node *node,
    struct vfs_stat *result
);

static struct vfs_node *initramfs_node_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length
);

static struct vfs_node *initramfs_node_parent(
    struct vfs_node *directory
);

// Static local variables
static const struct vfs_file_operations initramfs_file_operations = {
    .read =
        initramfs_file_read,
    .write =
        NULL,
    .destroy =
        initramfs_file_destroy,
};

static const struct vfs_node_operations initramfs_node_operations = {
    .open =
        initramfs_node_open,
    .stat =
        initramfs_node_stat,
    .lookup =
        initramfs_node_lookup,
    .parent =
        initramfs_node_parent,
    .destroy =
        NULL,
};

// Public functions implementations
enum initramfs_mount_result initramfs_mount(
    const void *archive,
    size_t archive_size,
    struct initramfs **result)
{
    if (
        archive == NULL ||
        result == NULL
    ) {
        return
            INITRAMFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    struct initramfs *filesystem =
        kmalloc(sizeof(*filesystem));

    if (filesystem == NULL) {
        return
            INITRAMFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
    }

    filesystem->archive =
        archive;

    filesystem->archive_size =
        archive_size;

    filesystem->nodes =
        NULL;

    filesystem->node_count =
        0;

    filesystem->root.parent =
        NULL;

    filesystem->root.first_child =
        NULL;

    filesystem->root.next_sibling =
        NULL;

    filesystem->root.next_all =
        NULL;

    filesystem->root.path =
        NULL;

    filesystem->root.path_length =
        0;

    filesystem->root.name =
        NULL;

    filesystem->root.name_length =
        0;

    filesystem->root.data =
        NULL;

    filesystem->root.size =
        0;

    if (!vfs_node_initialize(
        &filesystem->root.vfs,
        VFS_NODE_TYPE_DIRECTORY,
        &initramfs_node_operations,
        &filesystem->root
    )) {
        kfree(
            filesystem
        );

        return
            INITRAMFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
    }

    enum initramfs_mount_result materialize_result =
        initramfs_materialize_entries(
            filesystem
        );

    if (
        materialize_result !=
        INITRAMFS_MOUNT_RESULT_MOUNTED
    ) {
        initramfs_destroy(
            filesystem
        );

        return
            materialize_result;
    }

    enum initramfs_mount_result link_result =
        initramfs_link_tree(
            filesystem
        );

    if (
        link_result !=
        INITRAMFS_MOUNT_RESULT_MOUNTED
    ) {
        initramfs_destroy(
            filesystem
        );

        return
            link_result;
    }

    *result =
        filesystem;

    return
        INITRAMFS_MOUNT_RESULT_MOUNTED;
}

struct vfs_node *initramfs_root(
    const struct initramfs *filesystem)
{
    if (
        filesystem == NULL ||
        filesystem->root.vfs.reference_count == 0
    ) {
        return NULL;
    }

    return
        (struct vfs_node *)
        &filesystem->root.vfs;
}

bool initramfs_unmount(
    struct initramfs *filesystem)
{
    if (filesystem == NULL) {
        return false;
    }

    if (initramfs_has_external_references(
        filesystem
    )) {
        return false;
    }

    initramfs_destroy(
        filesystem
    );

    return true;
}

// Private functions and helpers implementations
static enum initramfs_mount_result initramfs_format_result_map(
    enum initramfs_format_result result)
{
    switch (result) {
        case INITRAMFS_FORMAT_RESULT_MALFORMED:
            return
                INITRAMFS_MOUNT_RESULT_MALFORMED_ARCHIVE;

        case INITRAMFS_FORMAT_RESULT_UNSUPPORTED_TYPE:
            return
                INITRAMFS_MOUNT_RESULT_UNSUPPORTED_ARCHIVE;

        case INITRAMFS_FORMAT_RESULT_INVALID_ARGUMENT:
            return
                INITRAMFS_MOUNT_RESULT_MALFORMED_ARCHIVE;

        case INITRAMFS_FORMAT_RESULT_ENTRY:
        case INITRAMFS_FORMAT_RESULT_END:
            break;
    }

    return
        INITRAMFS_MOUNT_RESULT_MALFORMED_ARCHIVE;
}

static enum vfs_node_type initramfs_vfs_type(
    enum initramfs_format_entry_type type)
{
    switch (type) {
        case INITRAMFS_FORMAT_ENTRY_REGULAR_FILE:
            return
                VFS_NODE_TYPE_REGULAR_FILE;

        case INITRAMFS_FORMAT_ENTRY_DIRECTORY:
            return
                VFS_NODE_TYPE_DIRECTORY;
    }

    return
        VFS_NODE_TYPE_REGULAR_FILE;
}

static bool initramfs_path_equal(
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

static void initramfs_path_basename(
    const char *path,
    size_t path_length,
    const char **name,
    size_t *name_length)
{
    if (
        path == NULL ||
        path_length == 0 ||
        name == NULL ||
        name_length == NULL
    ) {
        kernel_panic(
            "Initramfs basename received invalid path"
        );
    }

    size_t start =
        0;

    for (
        size_t index = 0;
        index < path_length;
        ++index
    ) {
        if (path[index] == '/') {
            start =
                index + 1U;
        }
    }

    *name =
        path + start;

    *name_length =
        path_length - start;
}

static bool initramfs_path_parent_length(
    const char *path,
    size_t path_length,
    size_t *parent_length)
{
    if (
        path == NULL ||
        path_length == 0 ||
        parent_length == NULL
    ) {
        return false;
    }

    bool found =
        false;

    size_t result =
        0;

    for (
        size_t index = 0;
        index < path_length;
        ++index
    ) {
        if (path[index] == '/') {
            found =
                true;

            result =
                index;
        }
    }

    if (found) {
        *parent_length =
            result;
    }

    return
        found;
}

static struct initramfs_node *initramfs_find_path(
    const struct initramfs *filesystem,
    const char *path,
    size_t path_length)
{
    if (
        filesystem == NULL ||
        path == NULL ||
        path_length == 0
    ) {
        return NULL;
    }

    for (
        struct initramfs_node *node =
            filesystem->nodes;
        node != NULL;
        node = node->next_all
    ) {
        if (initramfs_path_equal(
            node->path,
            node->path_length,
            path,
            path_length
        )) {
            return
                node;
        }
    }

    return NULL;
}

static enum initramfs_mount_result initramfs_materialize_entries(
    struct initramfs *filesystem)
{
    if (
        filesystem == NULL ||
        filesystem->archive == NULL
    ) {
        return
            INITRAMFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    struct initramfs_format_iterator iterator;

    if (!initramfs_format_iterator_initialize(
        filesystem->archive,
        filesystem->archive_size,
        &iterator
    )) {
        return
            INITRAMFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    while (true) {
        struct initramfs_format_entry entry;

        enum initramfs_format_result format_result =
            initramfs_format_next(
                &iterator,
                &entry
            );

        if (
            format_result ==
            INITRAMFS_FORMAT_RESULT_END
        ) {
            return
                INITRAMFS_MOUNT_RESULT_MOUNTED;
        }

        if (
            format_result !=
            INITRAMFS_FORMAT_RESULT_ENTRY
        ) {
            return
                initramfs_format_result_map(
                    format_result
                );
        }

        if (
            initramfs_find_path(
                filesystem,
                entry.path,
                entry.path_length
            ) != NULL
        ) {
            return
                INITRAMFS_MOUNT_RESULT_MALFORMED_ARCHIVE;
        }

        struct initramfs_node *node =
            kmalloc(sizeof(*node));

        if (node == NULL) {
            return
                INITRAMFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
        }

        node->parent =
            NULL;

        node->first_child =
            NULL;

        node->next_sibling =
            NULL;

        node->next_all =
            NULL;

        node->path =
            entry.path;

        node->path_length =
            entry.path_length;

        initramfs_path_basename(
            entry.path,
            entry.path_length,
            &node->name,
            &node->name_length
        );

        node->data =
            entry.data;

        node->size =
            entry.size;

        if (!vfs_node_initialize(
            &node->vfs,
            initramfs_vfs_type(
                entry.type
            ),
            &initramfs_node_operations,
            node
        )) {
            kfree(
                node
            );

            return
                INITRAMFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
        }

        node->next_all =
            filesystem->nodes;

        filesystem->nodes =
            node;

        if (
            filesystem->node_count ==
            SIZE_MAX
        ) {
            /*
             * node is now part of the unpublished filesystem and will be
             * released by the mount rollback path.
             */
            return
                INITRAMFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
        }

        ++filesystem->node_count;
    }
}

static enum initramfs_mount_result initramfs_link_tree(
    struct initramfs *filesystem)
{
    if (filesystem == NULL) {
        return
            INITRAMFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    for (
        struct initramfs_node *node =
            filesystem->nodes;
        node != NULL;
        node = node->next_all
    ) {
        size_t parent_path_length;

        struct initramfs_node *parent;

        if (!initramfs_path_parent_length(
            node->path,
            node->path_length,
            &parent_path_length
        )) {
            parent =
                &filesystem->root;
        } else {
            parent =
                initramfs_find_path(
                    filesystem,
                    node->path,
                    parent_path_length
                );

            if (parent == NULL) {
                return
                    INITRAMFS_MOUNT_RESULT_MALFORMED_ARCHIVE;
            }
        }

        if (
            parent->vfs.type !=
            VFS_NODE_TYPE_DIRECTORY
        ) {
            return
                INITRAMFS_MOUNT_RESULT_MALFORMED_ARCHIVE;
        }

        node->parent =
            parent;

        node->next_sibling =
            parent->first_child;

        parent->first_child =
            node;
    }

    return
        INITRAMFS_MOUNT_RESULT_MOUNTED;
}

static bool initramfs_has_external_references(
    const struct initramfs *filesystem)
{
    if (filesystem == NULL) {
        return true;
    }

    if (
        filesystem->root.vfs.reference_count !=
        1
    ) {
        return true;
    }

    for (
        struct initramfs_node *node =
            filesystem->nodes;
        node != NULL;
        node = node->next_all
    ) {
        if (
            node->vfs.reference_count !=
            1
        ) {
            return true;
        }
    }

    return false;
}

static void initramfs_destroy(
    struct initramfs *filesystem)
{
    if (filesystem == NULL) {
        return;
    }

    struct initramfs_node *node =
        filesystem->nodes;

    while (node != NULL) {
        struct initramfs_node *next =
            node->next_all;

        /*
         * The filesystem owns exactly one reference to every materialized
         * node. Rollback and successful unmount must never reach teardown
         * while another owner still references a node.
         *
         * Checking the exact count before release prevents accidentally
         * freeing node storage while a live VFS reference remains.
         */
        if (
            node->vfs.reference_count !=
            1
        ) {
            kernel_panic(
                "Initramfs node still referenced during teardown"
            );
        }

        if (!vfs_node_release(
            &node->vfs
        )) {
            kernel_panic(
                "Initramfs node ownership corrupted during teardown"
            );
        }

        kfree(
            node
        );

        node =
            next;
    }

    filesystem->nodes =
        NULL;

    filesystem->node_count =
        0;

    if (
        filesystem->root.vfs.reference_count !=
        1
    ) {
        kernel_panic(
            "Initramfs root still referenced during teardown"
        );
    }

    if (!vfs_node_release(
        &filesystem->root.vfs
    )) {
        kernel_panic(
            "Initramfs root ownership corrupted during teardown"
        );
    }

    kfree(
        filesystem
    );
}

static struct initramfs_node *initramfs_node_from_vfs(
    struct vfs_node *node)
{
    if (
        node == NULL ||
        node->reference_count == 0 ||
        node->private_data == NULL
    ) {
        return NULL;
    }

    struct initramfs_node *candidate =
        node->private_data;

    if (
        &candidate->vfs !=
        node
    ) {
        return NULL;
    }

    return
        candidate;
}

static enum vfs_open_result initramfs_node_open(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result)
{
    if (
        node == NULL ||
        result == NULL
    ) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    struct initramfs_node *entry =
        initramfs_node_from_vfs(
            node
        );

    if (entry == NULL) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (
        node->type !=
        VFS_NODE_TYPE_REGULAR_FILE
    ) {
        return
            VFS_OPEN_RESULT_NOT_SUPPORTED;
    }

    /*
     * Initramfs is immutable. Any request carrying write permission is
     * rejected at open time rather than creating an OFD that can never honor
     * its advertised access mode.
     */
    if (
        (access &
            VFS_OPEN_ACCESS_WRITE) != 0
    ) {
        return
            VFS_OPEN_RESULT_ACCESS_DENIED;
    }

    if (
        access !=
        VFS_OPEN_ACCESS_READ
    ) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    struct initramfs_file *open_file =
        kmalloc(sizeof(*open_file));

    if (open_file == NULL) {
        return
            VFS_OPEN_RESULT_RESOURCE_EXHAUSTED;
    }

    if (!vfs_file_initialize(
        &open_file->vfs,
        node,
        access,
        &initramfs_file_operations,
        open_file
    )) {
        kfree(
            open_file
        );

        return
            VFS_OPEN_RESULT_RESOURCE_EXHAUSTED;
    }

    *result =
        &open_file->vfs;

    return
        VFS_OPEN_RESULT_OPENED;
}

static enum vfs_io_result initramfs_file_read(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    if (
        file == NULL ||
        buffer == NULL ||
        bytes_read == NULL ||
        file->node == NULL
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        file->reference_count == 0 ||
        file->node->reference_count == 0 ||
        file->node->type !=
            VFS_NODE_TYPE_REGULAR_FILE
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    struct initramfs_node *entry =
        initramfs_node_from_vfs(
            file->node
        );

    if (entry == NULL) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    /*
     * The generic VFS does not call read callbacks for zero-length requests,
     * but keep the filesystem callback internally well-defined as well.
     */
    if (size == 0) {
        *bytes_read =
            0;

        return
            VFS_IO_RESULT_SUCCESS;
    }

    /*
     * All archive-backed file sizes originate in a size_t field. Therefore an
     * offset greater than SIZE_MAX is necessarily beyond EOF.
     */
    if (
        offset > SIZE_MAX ||
        (size_t) offset >=
            entry->size
    ) {
        *bytes_read =
            0;

        return
            VFS_IO_RESULT_SUCCESS;
    }

    size_t start =
        (size_t) offset;

    size_t available =
        entry->size -
        start;

    size_t transfer =
        size < available
            ? size
            : available;

    if (
        transfer != 0 &&
        entry->data == NULL
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    memcpy(
        buffer,
        entry->data + start,
        transfer
    );

    *bytes_read =
        transfer;

    return
        VFS_IO_RESULT_SUCCESS;
}

static void initramfs_file_destroy(
    struct vfs_file *file)
{
    if (file == NULL) {
        kernel_panic(
            "Initramfs file destroy received null file"
        );
    }

    struct initramfs_file *open_file =
        file->private_data;

    if (
        open_file == NULL ||
        &open_file->vfs != file
    ) {
        kernel_panic(
            "Initramfs open-file ownership corrupted"
        );
    }

    /*
     * vfs_file_release() owns and releases file->node after this callback.
     * Only the OFD storage belongs to the filesystem here.
     */
    kfree(
        open_file
    );
}

static enum vfs_stat_result initramfs_node_stat(
    struct vfs_node *node,
    struct vfs_stat *result)
{
    if (
        node == NULL ||
        result == NULL
    ) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    struct initramfs_node *entry =
        initramfs_node_from_vfs(
            node
        );

    if (entry == NULL) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    result->type =
        node->type;

    switch (node->type) {
        case VFS_NODE_TYPE_DIRECTORY:
            result->size =
                0;

            return
                VFS_STAT_RESULT_SUCCESS;

        case VFS_NODE_TYPE_REGULAR_FILE:
            result->size =
                entry->size;

            return
                VFS_STAT_RESULT_SUCCESS;

        case VFS_NODE_TYPE_CHARACTER_DEVICE:
            break;
    }

    return
        VFS_STAT_RESULT_INVALID_ARGUMENT;
}

static struct vfs_node *initramfs_node_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length)
{
    if (
        directory == NULL ||
        name == NULL
    ) {
        return NULL;
    }

    struct initramfs_node *entry =
        initramfs_node_from_vfs(
            directory
        );

    if (
        entry == NULL ||
        directory->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return NULL;
    }

    for (
        struct initramfs_node *child =
            entry->first_child;
        child != NULL;
        child = child->next_sibling
    ) {
        if (initramfs_path_equal(
            child->name,
            child->name_length,
            name,
            name_length
        )) {
            return
                &child->vfs;
        }
    }

    return NULL;
}

static struct vfs_node *initramfs_node_parent(
    struct vfs_node *directory)
{
    struct initramfs_node *entry =
        initramfs_node_from_vfs(
            directory
        );

    if (
        entry == NULL ||
        directory->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        entry->parent == NULL
    ) {
        return NULL;
    }

    return
        &entry->parent->vfs;
}

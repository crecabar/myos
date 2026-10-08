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

struct initramfs_external_entry;

struct initramfs_node {
    struct vfs_node vfs;

    struct initramfs_node *parent;

    struct initramfs_node *first_child;
    struct initramfs_node *next_sibling;

    struct initramfs_node *next_all;

    struct initramfs_external_entry *first_external_child;

    uint64_t children_generation;

    /*
     * Archive-backed names borrow bytes from the CPIO image. Kernel-created
     * directories instead own this optional name allocation.
     */
    char *owned_name;

    const char *path;
    size_t path_length;

    const char *name;
    size_t name_length;

    const uint8_t *data;
    size_t size;
};

struct initramfs_file {
    struct vfs_file vfs;

    struct initramfs_node *directory;

    struct initramfs_node *next_child;

    struct initramfs_external_entry *next_external_child;

    uint64_t directory_generation;
};

struct initramfs_external_entry {
    struct initramfs_external_entry *next_in_directory;
    struct initramfs_external_entry *next_all;

    struct vfs_node *node;

    size_t name_length;

    char name[
        VFS_NAME_MAX + 1U
    ];
};

struct initramfs {
    const uint8_t *archive;
    size_t archive_size;

    struct initramfs_node root;

    struct initramfs_node *nodes;
    size_t node_count;

    struct initramfs_external_entry *external_entries;
    size_t external_entry_count;
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

static enum vfs_directory_read_result initramfs_directory_read(
    struct vfs_file *file,
    struct vfs_directory_entry *result
);

static void initramfs_file_destroy(
    struct vfs_file *file
);

static enum vfs_stat_result initramfs_node_stat(
    struct vfs_node *node,
    struct vfs_stat *result
);

static enum vfs_lookup_result initramfs_node_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result
);

static struct vfs_node *initramfs_node_parent(
    struct vfs_node *directory
);

static bool initramfs_name_valid(
    const char *name,
    size_t name_length
);

static struct initramfs_node *initramfs_owned_node_from_vfs(
    const struct initramfs *filesystem,
    struct vfs_node *node
);

static bool initramfs_directory_contains_name(
    const struct initramfs_node *directory,
    const char *name,
    size_t name_length
);

// Static local variables
static const struct vfs_file_operations
    initramfs_regular_file_operations = {
        .read =
            initramfs_file_read,
        .write =
            NULL,
        .read_directory =
            NULL,
        .destroy =
            initramfs_file_destroy,
    };

static const struct vfs_file_operations
    initramfs_directory_file_operations = {
        .read =
            NULL,
        .write =
            NULL,
        .read_directory =
            initramfs_directory_read,
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

    filesystem->external_entries =
        NULL;

    filesystem->external_entry_count =
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

    filesystem->root.first_external_child =
        NULL;

    filesystem->root.children_generation =
        0;

    filesystem->root.owned_name =
        NULL;

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

enum initramfs_namespace_result initramfs_create_directory(
    struct initramfs *filesystem,
    struct vfs_node *parent,
    const char *name,
    size_t name_length,
    struct vfs_node **result)
{
    if (
        filesystem == NULL ||
        parent == NULL ||
        result == NULL ||
        !initramfs_name_valid(
            name,
            name_length
        )
    ) {
        return
            INITRAMFS_NAMESPACE_RESULT_INVALID_ARGUMENT;
    }

    struct initramfs_node *parent_entry =
        initramfs_owned_node_from_vfs(
            filesystem,
            parent
        );

    if (parent_entry == NULL) {
        return
            INITRAMFS_NAMESPACE_RESULT_INVALID_ARGUMENT;
    }

    if (
        parent->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            INITRAMFS_NAMESPACE_RESULT_NOT_DIRECTORY;
    }

    if (initramfs_directory_contains_name(
        parent_entry,
        name,
        name_length
    )) {
        return
            INITRAMFS_NAMESPACE_RESULT_ALREADY_EXISTS;
    }

    if (
        filesystem->node_count ==
            SIZE_MAX
    ) {
        return
            INITRAMFS_NAMESPACE_RESULT_RESOURCE_EXHAUSTED;
    }

    if (
        parent_entry->children_generation ==
            UINT64_MAX
    ) {
        return
            INITRAMFS_NAMESPACE_RESULT_RESOURCE_EXHAUSTED;
    }

    struct initramfs_node *node =
        kmalloc(sizeof(*node));

    if (node == NULL) {
        return
            INITRAMFS_NAMESPACE_RESULT_RESOURCE_EXHAUSTED;
    }

    char *owned_name =
        kmalloc(
            name_length + 1U
        );

    if (owned_name == NULL) {
        kfree(
            node
        );

        return
            INITRAMFS_NAMESPACE_RESULT_RESOURCE_EXHAUSTED;
    }

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        owned_name[index] =
            name[index];
    }

    owned_name[
        name_length
    ] = '\0';

    node->parent =
        parent_entry;

    node->first_child =
        NULL;

    node->next_sibling =
        parent_entry->first_child;

    node->next_all =
        NULL;

    node->first_external_child =
        NULL;

    node->children_generation =
        0;

    node->owned_name =
        owned_name;

    /*
     * Kernel-created directories are namespace objects rather than
     * archive-backed entries, so no archive pathname is required.
     */
    node->path =
        NULL;

    node->path_length =
        0;

    node->name =
        owned_name;

    node->name_length =
        name_length;

    node->data =
        NULL;

    node->size =
        0;

    if (!vfs_node_initialize(
        &node->vfs,
        VFS_NODE_TYPE_DIRECTORY,
        &initramfs_node_operations,
        node
    )) {
        kfree(
            owned_name
        );

        kfree(
            node
        );

        return
            INITRAMFS_NAMESPACE_RESULT_RESOURCE_EXHAUSTED;
    }

    /*
     * Publish only after every allocation and initialization has succeeded.
     */
    node->next_all =
        filesystem->nodes;

    filesystem->nodes =
        node;

    parent_entry->first_child =
        node;

    ++parent_entry->children_generation;

    ++filesystem->node_count;

    *result =
        &node->vfs;

    return
        INITRAMFS_NAMESPACE_RESULT_SUCCESS;
}

enum initramfs_namespace_result initramfs_attach_leaf(
    struct initramfs *filesystem,
    struct vfs_node *parent,
    const char *name,
    size_t name_length,
    struct vfs_node *child)
{
    if (
        filesystem == NULL ||
        parent == NULL ||
        child == NULL ||
        child->reference_count == 0 ||
        !initramfs_name_valid(
            name,
            name_length
        )
    ) {
        return
            INITRAMFS_NAMESPACE_RESULT_INVALID_ARGUMENT;
    }

    struct initramfs_node *parent_entry =
        initramfs_owned_node_from_vfs(
            filesystem,
            parent
        );

    if (parent_entry == NULL) {
        return
            INITRAMFS_NAMESPACE_RESULT_INVALID_ARGUMENT;
    }

    if (
        parent->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            INITRAMFS_NAMESPACE_RESULT_NOT_DIRECTORY;
    }

    /*
     * Directory grafting requires mountpoint semantics, especially correct
     * handling of ".." across filesystem boundaries. Do not approximate that
     * behavior with this bootstrap leaf facility.
     */
    if (
        child->type ==
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            INITRAMFS_NAMESPACE_RESULT_NOT_SUPPORTED;
    }

    /*
     * Aliasing one initramfs-owned node back into the same filesystem would
     * complicate ownership and teardown without serving the bootstrap use
     * case. Archive and kernel-created nodes already have their canonical
     * namespace location.
     */
    if (
        initramfs_owned_node_from_vfs(
            filesystem,
            child
        ) != NULL
    ) {
        return
            INITRAMFS_NAMESPACE_RESULT_INVALID_ARGUMENT;
    }

    if (initramfs_directory_contains_name(
        parent_entry,
        name,
        name_length
    )) {
        return
            INITRAMFS_NAMESPACE_RESULT_ALREADY_EXISTS;
    }

    if (
        filesystem->external_entry_count ==
            SIZE_MAX
    ) {
        return
            INITRAMFS_NAMESPACE_RESULT_RESOURCE_EXHAUSTED;
    }

    if (
        parent_entry->children_generation ==
            UINT64_MAX
    ) {
        return
            INITRAMFS_NAMESPACE_RESULT_RESOURCE_EXHAUSTED;
    }

    struct initramfs_external_entry *entry =
        kmalloc(sizeof(*entry));

    if (entry == NULL) {
        return
            INITRAMFS_NAMESPACE_RESULT_RESOURCE_EXHAUSTED;
    }

    if (!vfs_node_retain(
        child
    )) {
        kfree(
            entry
        );

        return
            INITRAMFS_NAMESPACE_RESULT_INVALID_ARGUMENT;
    }

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        entry->name[index] =
            name[index];
    }

    entry->name[
        name_length
    ] = '\0';

    entry->name_length =
        name_length;

    entry->node =
        child;

    entry->next_in_directory =
        parent_entry->first_external_child;

    entry->next_all =
        filesystem->external_entries;

    parent_entry->first_external_child =
        entry;

    ++parent_entry->children_generation;

    filesystem->external_entries =
        entry;

    ++filesystem->external_entry_count;

    return
        INITRAMFS_NAMESPACE_RESULT_SUCCESS;
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

static bool initramfs_name_valid(
    const char *name,
    size_t name_length)
{
    if (
        name == NULL ||
        name_length == 0 ||
        name_length > VFS_NAME_MAX
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        if (
            name[index] == '\0' ||
            name[index] == '/'
        ) {
            return false;
        }
    }

    if (
        name_length == 1 &&
        name[0] == '.'
    ) {
        return false;
    }

    if (
        name_length == 2 &&
        name[0] == '.' &&
        name[1] == '.'
    ) {
        return false;
    }

    return true;
}

static struct initramfs_node *initramfs_owned_node_from_vfs(
    const struct initramfs *filesystem,
    struct vfs_node *node)
{
    if (
        filesystem == NULL ||
        node == NULL ||
        node->reference_count == 0
    ) {
        return NULL;
    }

    if (
        &filesystem->root.vfs ==
        node
    ) {
        return
            (struct initramfs_node *)
            &filesystem->root;
    }

    for (
        struct initramfs_node *candidate =
            filesystem->nodes;
        candidate != NULL;
        candidate = candidate->next_all
    ) {
        if (
            &candidate->vfs ==
            node
        ) {
            return
                candidate;
        }
    }

    return NULL;
}

static bool initramfs_directory_contains_name(
    const struct initramfs_node *directory,
    const char *name,
    size_t name_length)
{
    if (
        directory == NULL ||
        name == NULL ||
        directory->vfs.type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return false;
    }

    for (
        const struct initramfs_node *child =
            directory->first_child;
        child != NULL;
        child = child->next_sibling
    ) {
        if (initramfs_path_equal(
            child->name,
            child->name_length,
            name,
            name_length
        )) {
            return true;
        }
    }

    for (
        const struct initramfs_external_entry *entry =
            directory->first_external_child;
        entry != NULL;
        entry = entry->next_in_directory
    ) {
        if (initramfs_path_equal(
            entry->name,
            entry->name_length,
            name,
            name_length
        )) {
            return true;
        }
    }

    return false;
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

        node->first_external_child =
            NULL;

        node->children_generation =
            0;

        node->owned_name =
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

    struct initramfs_external_entry *external =
        filesystem->external_entries;

    while (external != NULL) {
        struct initramfs_external_entry *next =
            external->next_all;

        /*
        * The filesystem owns one reference acquired when this external leaf
        * was attached. Other owners may legitimately keep the external node
        * alive after the initramfs namespace disappears.
        */
        if (!vfs_node_release(
            external->node
        )) {
            kernel_panic(
                "Initramfs external node ownership corrupted during teardown"
            );
        }

        kfree(
            external
        );

        external =
            next;
    }

    filesystem->external_entries =
        NULL;

    filesystem->external_entry_count =
        0;

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

        if (
            node->owned_name != NULL
        ) {
            kfree(
                node->owned_name
            );

            node->owned_name =
                NULL;
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

    /*
     * Initramfs is immutable from ordinary file-I/O users. Namespace
     * bootstrap mutation is performed through explicit kernel APIs, never
     * through writable open-file descriptions.
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

    const struct vfs_file_operations *operations;

    switch (node->type) {
        case VFS_NODE_TYPE_REGULAR_FILE:
            operations =
                &initramfs_regular_file_operations;
            break;

        case VFS_NODE_TYPE_DIRECTORY:
            operations =
                &initramfs_directory_file_operations;
            break;

        case VFS_NODE_TYPE_CHARACTER_DEVICE:
        default:
            return
                VFS_OPEN_RESULT_NOT_SUPPORTED;
    }

    struct initramfs_file *open_file =
        kmalloc(sizeof(*open_file));

    if (open_file == NULL) {
        return
            VFS_OPEN_RESULT_RESOURCE_EXHAUSTED;
    }

    open_file->directory =
        NULL;

    open_file->next_child =
        NULL;

    open_file->next_external_child =
        NULL;

    open_file->directory_generation =
        0;

    if (
        node->type ==
            VFS_NODE_TYPE_DIRECTORY
    ) {
        open_file->directory =
            entry;

        open_file->next_child =
            entry->first_child;

        open_file->next_external_child =
            entry->first_external_child;

        open_file->directory_generation =
            entry->children_generation;
    }

    if (!vfs_file_initialize(
        &open_file->vfs,
        node,
        access,
        operations,
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

static enum vfs_directory_read_result initramfs_directory_read(
    struct vfs_file *file,
    struct vfs_directory_entry *result)
{
    if (
        file == NULL ||
        result == NULL ||
        file->reference_count == 0 ||
        file->node == NULL ||
        file->node->reference_count == 0 ||
        file->node->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        file->private_data == NULL
    ) {
        return
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
    }

    struct initramfs_file *open_file =
        file->private_data;

    struct initramfs_node *directory =
        initramfs_node_from_vfs(
            file->node
        );

    if (
        directory == NULL ||
        &open_file->vfs != file ||
        open_file->directory != directory
    ) {
        return
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Directory streams describe one stable namespace generation.
     * Bootstrap namespace mutation after open invalidates the stream instead
     * of silently producing a mixture of old and new contents.
     */
    if (
        open_file->directory_generation !=
            directory->children_generation
    ) {
        return
            VFS_DIRECTORY_READ_RESULT_INVALIDATED;
    }

    if (
        open_file->next_child != NULL
    ) {
        struct initramfs_node *child =
            open_file->next_child;

        if (
            !initramfs_name_valid(
                child->name,
                child->name_length
            )
        ) {
            return
                VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
        }

        open_file->next_child =
            child->next_sibling;

        result->type =
            child->vfs.type;

        result->name_length =
            child->name_length;

        for (
            size_t index = 0;
            index < child->name_length;
            ++index
        ) {
            result->name[index] =
                child->name[index];
        }

        result->name[
            child->name_length
        ] = '\0';

        return
            VFS_DIRECTORY_READ_RESULT_ENTRY;
    }

    if (
        open_file->next_external_child != NULL
    ) {
        struct initramfs_external_entry *external =
            open_file->next_external_child;

        if (
            external->node == NULL ||
            external->node->reference_count == 0 ||
            !initramfs_name_valid(
                external->name,
                external->name_length
            )
        ) {
            return
                VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
        }

        open_file->next_external_child =
            external->next_in_directory;

        result->type =
            external->node->type;

        result->name_length =
            external->name_length;

        for (
            size_t index = 0;
            index < external->name_length;
            ++index
        ) {
            result->name[index] =
                external->name[index];
        }

        result->name[
            external->name_length
        ] = '\0';

        return
            VFS_DIRECTORY_READ_RESULT_ENTRY;
    }

    return
        VFS_DIRECTORY_READ_RESULT_END;
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

static enum vfs_lookup_result initramfs_node_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result)
{
    if (
        directory == NULL ||
        name == NULL ||
        result == NULL
    ) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
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
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
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
            *result =
                &child->vfs;

            return
                VFS_LOOKUP_RESULT_FOUND;
        }
    }

    for (
        struct initramfs_external_entry *external =
            entry->first_external_child;
        external != NULL;
        external = external->next_in_directory
    ) {
        if (initramfs_path_equal(
            external->name,
            external->name_length,
            name,
            name_length
        )) {
            *result =
                external->node;

            return
                VFS_LOOKUP_RESULT_FOUND;
        }
    }

    return
        VFS_LOOKUP_RESULT_NOT_FOUND;
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

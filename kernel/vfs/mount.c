// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file mount.c
 * @brief Generic VFS mount topology implementation.
 */

#include "mount.h"

#include <stddef.h>

// Private functions and helpers declarations
static bool vfs_mount_table_valid(
    const struct vfs_mount_table *table
);

static bool vfs_mount_node_valid(
    const struct vfs_node *node
);

static bool vfs_mount_table_conflicts(
    const struct vfs_mount_table *table,
    const struct vfs_node *mountpoint,
    const struct vfs_node *mounted_root
);

// Public functions implementations
bool vfs_mount_table_initialize(
    struct vfs_mount_table *table)
{
    if (table == NULL) {
        return false;
    }

    table->count =
        0;

    for (size_t index = 0;
         index < VFS_MOUNT_TABLE_CAPACITY;
         ++index) {
        table->entries[index] =
            (struct vfs_mount_entry) {
                .mountpoint = NULL,
                .mounted_root = NULL,
            };
    }

    return true;
}

enum vfs_mount_result vfs_mount_table_attach(
    struct vfs_mount_table *table,
    struct vfs_node *mountpoint,
    struct vfs_node *mounted_root)
{
    if (
        !vfs_mount_table_valid(
            table
        ) ||
        !vfs_mount_node_valid(
            mountpoint
        ) ||
        !vfs_mount_node_valid(
            mounted_root
        ) ||
        mountpoint == mounted_root
    ) {
        return
            VFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    if (vfs_mount_table_conflicts(
        table,
        mountpoint,
        mounted_root
    )) {
        return
            VFS_MOUNT_RESULT_CONFLICT;
    }

    if (
        table->count ==
        VFS_MOUNT_TABLE_CAPACITY
    ) {
        return
            VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
    }

    /*
     * Acquire both references before publishing the relation. Failure while
     * acquiring the second reference must roll back the first one so attach is
     * atomic from the caller's point of view.
     */
    if (!vfs_node_retain(
        mountpoint
    )) {
        return
            VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
    }

    if (!vfs_node_retain(
        mounted_root
    )) {
        (void) vfs_node_release(
            mountpoint
        );

        return
            VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
    }

    table->entries[table->count] =
        (struct vfs_mount_entry) {
            .mountpoint = mountpoint,
            .mounted_root = mounted_root,
        };

    ++table->count;

    return
        VFS_MOUNT_RESULT_SUCCESS;
}

enum vfs_mount_result vfs_mount_table_detach(
    struct vfs_mount_table *table,
    struct vfs_node *mountpoint)
{
    if (
        !vfs_mount_table_valid(
            table
        ) ||
        !vfs_mount_node_valid(
            mountpoint
        )
    ) {
        return
            VFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    size_t found =
        table->count;

    for (size_t index = 0;
         index < table->count;
         ++index) {
        if (
            table->entries[index].mountpoint ==
            mountpoint
        ) {
            found =
                index;

            break;
        }
    }

    if (found == table->count) {
        return
            VFS_MOUNT_RESULT_NOT_FOUND;
    }

    struct vfs_node *owned_mountpoint =
        table->entries[found].mountpoint;

    struct vfs_node *owned_mounted_root =
        table->entries[found].mounted_root;

    /*
     * Remove the relation before releasing either owned reference. A final
     * release is allowed to destroy filesystem-specific node storage, so no
     * table entry may retain a pointer once ownership is relinquished.
     */
    for (size_t index = found;
         index + 1 < table->count;
         ++index) {
        table->entries[index] =
            table->entries[index + 1];
    }

    --table->count;

    table->entries[table->count] =
        (struct vfs_mount_entry) {
            .mountpoint = NULL,
            .mounted_root = NULL,
        };

    /*
     * Both releases are guaranteed to consume references owned by this table.
     * vfs_mount_table_valid() established that each node was live before the
     * relation was unpublished.
     */
    (void) vfs_node_release(
        owned_mounted_root
    );

    (void) vfs_node_release(
        owned_mountpoint
    );

    return
        VFS_MOUNT_RESULT_SUCCESS;
}

enum vfs_mount_result vfs_mount_table_lookup_mounted_root(
    const struct vfs_mount_table *table,
    struct vfs_node *mountpoint,
    struct vfs_node **result)
{
    if (
        !vfs_mount_table_valid(
            table
        ) ||
        !vfs_mount_node_valid(
            mountpoint
        ) ||
        result == NULL
    ) {
        return
            VFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    for (size_t index = 0;
         index < table->count;
         ++index) {
        const struct vfs_mount_entry *entry =
            &table->entries[index];

        if (
            entry->mountpoint !=
            mountpoint
        ) {
            continue;
        }

        if (!vfs_node_retain(
            entry->mounted_root
        )) {
            return
                VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
        }

        *result =
            entry->mounted_root;

        return
            VFS_MOUNT_RESULT_SUCCESS;
    }

    return
        VFS_MOUNT_RESULT_NOT_FOUND;
}

enum vfs_mount_result vfs_mount_table_lookup_mountpoint(
    const struct vfs_mount_table *table,
    struct vfs_node *mounted_root,
    struct vfs_node **result)
{
    if (
        !vfs_mount_table_valid(
            table
        ) ||
        !vfs_mount_node_valid(
            mounted_root
        ) ||
        result == NULL
    ) {
        return
            VFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    for (size_t index = 0;
         index < table->count;
         ++index) {
        const struct vfs_mount_entry *entry =
            &table->entries[index];

        if (
            entry->mounted_root !=
            mounted_root
        ) {
            continue;
        }

        if (!vfs_node_retain(
            entry->mountpoint
        )) {
            return
                VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
        }

        *result =
            entry->mountpoint;

        return
            VFS_MOUNT_RESULT_SUCCESS;
    }

    return
        VFS_MOUNT_RESULT_NOT_FOUND;
}

// Private functions and helpers implementations
static bool vfs_mount_table_valid(
    const struct vfs_mount_table *table)
{
    if (
        table == NULL ||
        table->count >
            VFS_MOUNT_TABLE_CAPACITY
    ) {
        return false;
    }

    for (size_t index = 0;
         index < table->count;
         ++index) {
        if (
            !vfs_mount_node_valid(
                table->entries[index].mountpoint
            ) ||
            !vfs_mount_node_valid(
                table->entries[index].mounted_root
            )
        ) {
            return false;
        }
    }

    return true;
}

static bool vfs_mount_node_valid(
    const struct vfs_node *node)
{
    return
        node != NULL &&
        node->reference_count != 0 &&
        node->type ==
            VFS_NODE_TYPE_DIRECTORY;
}

static bool vfs_mount_table_conflicts(
    const struct vfs_mount_table *table,
    const struct vfs_node *mountpoint,
    const struct vfs_node *mounted_root)
{
    for (size_t index = 0;
         index < table->count;
         ++index) {
        const struct vfs_mount_entry *entry =
            &table->entries[index];

        if (
            entry->mountpoint == mountpoint ||
            entry->mounted_root == mounted_root ||
            entry->mountpoint == mounted_root ||
            entry->mounted_root == mountpoint
        ) {
            return true;
        }
    }

    return false;
}

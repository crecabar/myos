// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file mount.h
 * @brief Generic VFS mount topology.
 *
 * A VFS mount relates one covered directory node from an existing namespace
 * tree to the root directory of another filesystem.
 *
 * The relation belongs to the VFS rather than either filesystem. Filesystems
 * therefore do not need to manufacture cross-filesystem parent links or know
 * where they are mounted.
 *
 * A mount table owns one independent reference to each covered mountpoint and
 * one independent reference to each mounted root for the lifetime of the
 * relation.
 *
 * The initial implementation deliberately supports a bounded number of
 * non-stacked mounts. Nested mounts remain possible when the nested mountpoint
 * is an ordinary child inside another mounted filesystem.
 */

#ifndef MYOS_VFS_MOUNT_H
#define MYOS_VFS_MOUNT_H

#include "vfs.h"

#include <stdbool.h>
#include <stddef.h>

#define VFS_MOUNT_TABLE_CAPACITY 16U

enum vfs_mount_result {
    VFS_MOUNT_RESULT_SUCCESS,
    VFS_MOUNT_RESULT_NOT_FOUND,
    VFS_MOUNT_RESULT_INVALID_ARGUMENT,
    VFS_MOUNT_RESULT_CONFLICT,
    VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED,
};

/**
 * Describes one VFS mount relation.
 *
 * mountpoint is the directory hidden by the mount.
 * mounted_root is the root directory exposed at that location.
 *
 * Both pointers are owned references held by the containing mount table.
 */
struct vfs_mount_entry {
    struct vfs_node *mountpoint;
    struct vfs_node *mounted_root;
};

/**
 * Owns the currently installed VFS mount relations.
 *
 * Entries in the interval [0, count) are live and each owns one reference to
 * both nodes. Remaining entries carry no ownership.
 *
 * Callers must initialize the table with vfs_mount_table_initialize() before
 * using any other mount operation.
 */
struct vfs_mount_table {
    struct vfs_mount_entry entries[
        VFS_MOUNT_TABLE_CAPACITY
    ];

    size_t count;
};

/**
 * Initializes an empty VFS mount table.
 *
 * The table acquires no resources.
 *
 * @param table Table storage supplied by the caller.
 *
 * @return true when initialized; false for invalid input.
 */
bool vfs_mount_table_initialize(
    struct vfs_mount_table *table
);

/**
 * Installs one mount relation.
 *
 * mountpoint and mounted_root must be distinct live directory nodes.
 *
 * On success, the table acquires one independent reference to each node.
 *
 * The initial topology intentionally rejects stacked or ambiguous mount
 * relations. A node already participating as either side of an existing mount
 * cannot be reused as either side of another relation. Ordinary descendant
 * directories remain valid mountpoints, so nested filesystems are still
 * representable.
 *
 * On failure, no ownership is transferred and the table remains unchanged.
 *
 * @param table Initialized mount table.
 * @param mountpoint Existing directory that becomes covered.
 * @param mounted_root Root directory exposed at mountpoint.
 *
 * @return Detailed mount result.
 */
enum vfs_mount_result vfs_mount_table_attach(
    struct vfs_mount_table *table,
    struct vfs_node *mountpoint,
    struct vfs_node *mounted_root
);

/**
 * Removes the relation installed at one mountpoint.
 *
 * On success, the table releases the references it owns to both the
 * mountpoint and mounted root. References owned by filesystems or callers are
 * unaffected.
 *
 * @param table Initialized mount table.
 * @param mountpoint Covered directory identifying the relation.
 *
 * @return Detailed unmount result.
 */
enum vfs_mount_result vfs_mount_table_detach(
    struct vfs_mount_table *table,
    struct vfs_node *mountpoint
);

/**
 * Finds the mounted root exposed at one mountpoint.
 *
 * On success, result receives exactly one owned node reference. The caller
 * must eventually release it with vfs_node_release().
 *
 * On failure, result remains unchanged.
 *
 * @param table Initialized mount table.
 * @param mountpoint Covered directory to inspect.
 * @param result Receives one owned mounted-root reference.
 *
 * @return Detailed lookup result.
 */
enum vfs_mount_result vfs_mount_table_lookup_mounted_root(
    const struct vfs_mount_table *table,
    struct vfs_node *mountpoint,
    struct vfs_node **result
);

/**
 * Finds the covered mountpoint associated with one mounted root.
 *
 * This reverse relation is required when pathname traversal encounters ".."
 * while positioned at the root of a mounted filesystem.
 *
 * On success, result receives exactly one owned node reference. The caller
 * must eventually release it with vfs_node_release().
 *
 * On failure, result remains unchanged.
 *
 * @param table Initialized mount table.
 * @param mounted_root Mounted filesystem root to inspect.
 * @param result Receives one owned mountpoint reference.
 *
 * @return Detailed lookup result.
 */
enum vfs_mount_result vfs_mount_table_lookup_mountpoint(
    const struct vfs_mount_table *table,
    struct vfs_node *mounted_root,
    struct vfs_node **result
);

#endif

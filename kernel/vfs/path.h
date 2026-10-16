// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file path.h
 * @brief Generic VFS pathname resolution.
 *
 * Path resolution is independent of process state. Callers explicitly provide
 * the namespace root, the directory from which relative paths begin, and the
 * optional VFS mount topology through which traversal occurs.
 *
 * A NULL mount table describes a namespace without mounted filesystems.
 *
 * When root or start names a covered mountpoint, traversal begins from the
 * mounted root visible at that location. The current node-only pathname model
 * therefore represents the visible namespace rather than preserving hidden
 * access to a covered directory through an older node reference.
 *
 * Successful resolution returns exactly one owned vfs_node reference. The
 * caller must eventually release that reference with vfs_node_release().
 */

#ifndef MYOS_VFS_PATH_H
#define MYOS_VFS_PATH_H

#include "mount.h"
#include "vfs.h"

#include <stddef.h>

#define VFS_PATH_MAX 4096U

enum vfs_path_result {
    VFS_PATH_RESULT_FOUND,
    VFS_PATH_RESULT_NOT_FOUND,
    VFS_PATH_RESULT_INVALID_ARGUMENT,
    VFS_PATH_RESULT_NOT_DIRECTORY,
    VFS_PATH_RESULT_NOT_SUPPORTED,
    VFS_PATH_RESULT_RESOURCE_EXHAUSTED,
};

/**
 * Resolves one absolute or relative pathname.
 *
 * Absolute paths begin from the visible form of root. Relative paths begin
 * from the visible form of start.
 *
 * When mounts is non-NULL, ordinary descent transparently crosses from a
 * covered mountpoint into its mounted root. Parent traversal from a mounted
 * root crosses back through the covered mountpoint before resolving that
 * mountpoint's filesystem-provided parent.
 *
 * Namespace traversal remains bounded at the visible namespace root. In
 * particular, ".." at a mounted root that is itself the namespace root cannot
 * escape through the mount relation.
 *
 * Repeated '/' separators are ignored. "." preserves the current directory.
 * A trailing '/' requires the final resolved node to be a directory.
 *
 * path is not required to be NUL-terminated. Embedded NUL bytes are rejected.
 *
 * On success, result receives exactly one owned node reference. On failure,
 * result remains unchanged.
 *
 * @param root Namespace root directory.
 * @param start Directory from which relative paths begin.
 * @param mounts Optional VFS mount topology, or NULL for none.
 * @param path Path bytes.
 * @param path_length Number of path bytes.
 * @param result Receives one owned resolved node reference.
 *
 * @return Detailed pathname resolution result.
 */
enum vfs_path_result vfs_path_resolve(
    struct vfs_node *root,
    struct vfs_node *start,
    const struct vfs_mount_table *mounts,
    const char *path,
    size_t path_length,
    struct vfs_node **result
);

#endif

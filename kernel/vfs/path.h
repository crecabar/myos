// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file path.h
 * @brief Generic VFS pathname resolution.
 *
 * Path resolution is independent of process state and mounted filesystem
 * policy. Callers explicitly provide the namespace root and the directory from
 * which relative paths begin.
 *
 * Successful resolution returns exactly one owned vfs_node reference. The
 * caller must eventually release that reference with vfs_node_release().
 */

#ifndef MYOS_VFS_PATH_H
#define MYOS_VFS_PATH_H

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
 * Absolute paths begin from root. Relative paths begin from start.
 *
 * Repeated '/' separators are ignored. "." preserves the current directory.
 * ".." traverses through vfs_node_parent(), except at root where it remains
 * bounded to root.
 *
 * A trailing '/' requires the final resolved node to be a directory.
 *
 * path is not required to be NUL-terminated. Embedded NUL bytes are rejected.
 *
 * On success, result receives exactly one owned node reference. On failure,
 * result remains unchanged.
 *
 * @param root Namespace root directory.
 * @param start Directory from which relative paths begin.
 * @param path Path bytes.
 * @param path_length Number of path bytes.
 * @param result Receives one owned resolved node reference.
 *
 * @return Detailed pathname resolution result.
 */
enum vfs_path_result vfs_path_resolve(
    struct vfs_node *root,
    struct vfs_node *start,
    const char *path,
    size_t path_length,
    struct vfs_node **result
);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file path.h
 * @brief Process-relative pathname resolution and current-directory changes.
 */

#ifndef MYOS_PROCESS_PATH_H
#define MYOS_PROCESS_PATH_H

#include "instance.h"

#include "../vfs/vfs.h"

#include <stddef.h>

enum process_path_result {
    PROCESS_PATH_RESULT_RESOLVED,
    PROCESS_PATH_RESULT_NOT_FOUND,
    PROCESS_PATH_RESULT_INVALID_ARGUMENT,
    PROCESS_PATH_RESULT_NO_NAMESPACE_ROOT,
    PROCESS_PATH_RESULT_NO_CURRENT_DIRECTORY,
    PROCESS_PATH_RESULT_NOT_DIRECTORY,
    PROCESS_PATH_RESULT_NOT_SUPPORTED,
    PROCESS_PATH_RESULT_RESOURCE_EXHAUSTED,
};

enum process_getcwd_result {
    PROCESS_GETCWD_RESULT_SUCCESS,
    PROCESS_GETCWD_RESULT_INVALID_ARGUMENT,
    PROCESS_GETCWD_RESULT_NO_NAMESPACE_ROOT,
    PROCESS_GETCWD_RESULT_NO_CURRENT_DIRECTORY,
    PROCESS_GETCWD_RESULT_NOT_FOUND,
    PROCESS_GETCWD_RESULT_NOT_SUPPORTED,
    PROCESS_GETCWD_RESULT_ACCESS_DENIED,
    PROCESS_GETCWD_RESULT_INVALIDATED,
    PROCESS_GETCWD_RESULT_RESOURCE_EXHAUSTED,
    PROCESS_GETCWD_RESULT_BUFFER_TOO_SMALL,
    PROCESS_GETCWD_RESULT_PATH_TOO_LONG,
};

/**
 * Resolves a pathname in one process namespace context.
 *
 * Absolute paths begin at instance->namespace_root. Relative paths begin at
 * instance->current_directory, while traversal remains bounded by the process
 * namespace root.
 *
 * When instance->namespace_mounts is non-NULL, pathname traversal observes
 * that mount topology.
 *
 * The process must own a namespace root. Relative paths additionally require a
 * current working directory.
 *
 * On success, result receives exactly one owned vfs_node reference. The caller
 * must eventually release it with vfs_node_release().
 *
 * On failure, result remains unchanged.
 */
 enum process_path_result process_path_resolve(
    const struct process_instance *instance,
    const char *path,
    size_t path_length,
    struct vfs_node **result
);

/**
 * Changes one process instance's current working directory.
 *
 * The pathname is resolved inside the namespace owned by the process using
 * process_path_resolve(). The resolved object must be a directory.
 *
 * An absolute pathname can establish the first current directory of a process
 * that already owns a namespace root.
 *
 * On failure, the existing current directory remains unchanged.
 */
 enum process_path_result process_chdir(
    struct process_instance *instance,
    const char *path,
    size_t path_length
);

/**
 * Reconstructs one process instance's current working directory pathname.
 *
 * The result is an absolute pathname relative to the process namespace root.
 * Mounted filesystem roots are translated back through their covered
 * mountpoints, so the returned pathname describes the namespace-visible route
 * to the current directory.
 *
 * The pathname is reconstructed from VFS node identity. Parent directories
 * are enumerated to recover the ordinary component name referring to each
 * child.
 *
 * On success, buffer contains one NUL-terminated absolute pathname and
 * path_length receives the number of pathname bytes excluding the terminating
 * NUL.
 *
 * The root directory is represented as "/".
 *
 * A successful pathname never exceeds VFS_PATH_MAX bytes excluding the
 * terminating NUL.
 *
 * On failure, path_length remains unchanged. buffer contents are unspecified.
 *
 * @param instance Process instance whose current directory is inspected.
 * @param buffer Kernel buffer receiving the absolute pathname.
 * @param capacity Number of bytes available in buffer.
 * @param path_length Receives the pathname length excluding the terminating
 *        NUL.
 *
 * @return Detailed current-directory reconstruction result.
 */
enum process_getcwd_result process_getcwd(
    const struct process_instance *instance,
    char *buffer,
    size_t capacity,
    size_t *path_length
);

#endif

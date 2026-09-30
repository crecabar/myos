// SPDX-License-Identifier: GPL-2.0-only

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
    PROCESS_PATH_RESULT_NO_CURRENT_DIRECTORY,
    PROCESS_PATH_RESULT_NOT_DIRECTORY,
    PROCESS_PATH_RESULT_NOT_SUPPORTED,
    PROCESS_PATH_RESULT_RESOURCE_EXHAUSTED,
};

/**
 * Resolves a pathname in one process namespace context.
 *
 * Absolute paths begin at root and do not require the process to have a
 * current directory. Relative paths begin at instance->current_directory.
 *
 * On success, result receives exactly one owned vfs_node reference. The caller
 * must eventually release it with vfs_node_release().
 *
 * On failure, result remains unchanged.
 */
enum process_path_result process_path_resolve(
    const struct process_instance *instance,
    struct vfs_node *root,
    const char *path,
    size_t path_length,
    struct vfs_node **result
);

/**
 * Changes one process instance's current working directory.
 *
 * The pathname is resolved using process_path_resolve(). The resolved object
 * must be a directory.
 *
 * On failure, the existing current directory remains unchanged.
 */
enum process_path_result process_chdir(
    struct process_instance *instance,
    struct vfs_node *root,
    const char *path,
    size_t path_length
);

#endif

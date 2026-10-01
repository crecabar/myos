// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file path.c
 * @brief Process-relative pathname resolution implementation.
 */

 #include "path.h"

 #include "cwd.h"
 #include "namespace.h"

#include "../vfs/path.h"

#include <stddef.h>

// Private functions and helpers declarations
static enum process_path_result process_path_map_vfs_result(
    enum vfs_path_result result
);

// Public functions implementations
enum process_path_result process_path_resolve(
    const struct process_instance *instance,
    const char *path,
    size_t path_length,
    struct vfs_node **result)
{
    if (
        instance == NULL ||
        path == NULL ||
        result == NULL ||
        path_length == 0
    ) {
        return
            PROCESS_PATH_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Reject malformed lengths before consulting process namespace state.
     */
    if (path_length > VFS_PATH_MAX) {
        return
            PROCESS_PATH_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_node *root =
        process_namespace_root_get(
            instance
        );

    if (root == NULL) {
        return
            PROCESS_PATH_RESULT_NO_NAMESPACE_ROOT;
    }

    struct vfs_node *start;

    if (path[0] == '/') {
        start =
            root;
    } else {
        start =
            process_cwd_get(
                instance
            );

        if (start == NULL) {
            return
                PROCESS_PATH_RESULT_NO_CURRENT_DIRECTORY;
        }
    }

    enum vfs_path_result path_result =
        vfs_path_resolve(
            root,
            start,
            path,
            path_length,
            result
        );

    return
        process_path_map_vfs_result(
            path_result
        );
}

enum process_path_result process_chdir(
    struct process_instance *instance,
    const char *path,
    size_t path_length)
{
    if (instance == NULL) {
        return
            PROCESS_PATH_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_node *directory =
        NULL;

    enum process_path_result result =
        process_path_resolve(
            instance,
            path,
            path_length,
            &directory
        );

    if (
        result !=
        PROCESS_PATH_RESULT_RESOLVED
    ) {
        return
            result;
    }

    if (
        directory->type !=
        VFS_NODE_TYPE_DIRECTORY
    ) {
        (void) vfs_node_release(
            directory
        );

        return
            PROCESS_PATH_RESULT_NOT_DIRECTORY;
    }

    /*
     * process_cwd_set() retains the new directory before releasing the old
     * process-owned reference. directory itself remains owned by this
     * resolution until the temporary reference below is released.
     */
    if (!process_cwd_set(
        instance,
        directory
    )) {
        (void) vfs_node_release(
            directory
        );

        return
            PROCESS_PATH_RESULT_RESOURCE_EXHAUSTED;
    }

    /*
     * After process_cwd_set() succeeds there are at least two references:
     * the process-owned CWD and this resolver-owned temporary reference.
     * Releasing the latter cannot destroy the installed CWD.
     */
    (void) vfs_node_release(
        directory
    );

    return
        PROCESS_PATH_RESULT_RESOLVED;
}

// Private functions and helpers implementations
static enum process_path_result process_path_map_vfs_result(
    enum vfs_path_result result)
{
    switch (result) {
        case VFS_PATH_RESULT_FOUND:
            return
                PROCESS_PATH_RESULT_RESOLVED;

        case VFS_PATH_RESULT_NOT_FOUND:
            return
                PROCESS_PATH_RESULT_NOT_FOUND;

        case VFS_PATH_RESULT_INVALID_ARGUMENT:
            return
                PROCESS_PATH_RESULT_INVALID_ARGUMENT;

        case VFS_PATH_RESULT_NOT_DIRECTORY:
            return
                PROCESS_PATH_RESULT_NOT_DIRECTORY;

        case VFS_PATH_RESULT_NOT_SUPPORTED:
            return
                PROCESS_PATH_RESULT_NOT_SUPPORTED;

        case VFS_PATH_RESULT_RESOURCE_EXHAUSTED:
            return
                PROCESS_PATH_RESULT_RESOURCE_EXHAUSTED;
    }

    return
        PROCESS_PATH_RESULT_INVALID_ARGUMENT;
}

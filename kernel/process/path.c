// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

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

static enum process_getcwd_result process_getcwd_visible_root(
    struct vfs_node *root,
    const struct vfs_mount_table *mounts,
    struct vfs_node **result
);

static enum process_getcwd_result process_getcwd_parent_component(
    const struct vfs_mount_table *mounts,
    struct vfs_node *current,
    char *name,
    size_t *name_length,
    struct vfs_node **parent
);

static enum process_getcwd_result process_getcwd_find_child_name(
    struct vfs_node *parent,
    struct vfs_node *child,
    char *name,
    size_t *name_length
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

    const struct vfs_mount_table *mounts =
        process_namespace_mounts_get(
            instance
        );

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
            mounts,
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

enum process_getcwd_result process_getcwd(
    const struct process_instance *instance,
    char *buffer,
    size_t capacity,
    size_t *path_length)
{
    if (
        instance == NULL ||
        buffer == NULL ||
        path_length == NULL
    ) {
        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_node *namespace_root =
        process_namespace_root_get(
            instance
        );

    if (namespace_root == NULL) {
        return
            PROCESS_GETCWD_RESULT_NO_NAMESPACE_ROOT;
    }

    struct vfs_node *cwd =
        process_cwd_get(
            instance
        );

    if (cwd == NULL) {
        return
            PROCESS_GETCWD_RESULT_NO_CURRENT_DIRECTORY;
    }

    if (
        namespace_root->reference_count == 0 ||
        namespace_root->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        cwd->reference_count == 0 ||
        cwd->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    if (capacity == 0) {
        return
            PROCESS_GETCWD_RESULT_BUFFER_TOO_SMALL;
    }

    const struct vfs_mount_table *mounts =
        process_namespace_mounts_get(
            instance
        );

    struct vfs_node *visible_root =
        NULL;

    enum process_getcwd_result result =
        process_getcwd_visible_root(
            namespace_root,
            mounts,
            &visible_root
        );

    if (
        result !=
        PROCESS_GETCWD_RESULT_SUCCESS
    ) {
        return
            result;
    }

    if (!vfs_node_retain(
        cwd
    )) {
        (void) vfs_node_release(
            visible_root
        );

        return
            PROCESS_GETCWD_RESULT_RESOURCE_EXHAUSTED;
    }

    struct vfs_node *current =
        cwd;

    size_t start =
        capacity;

    --start;

    buffer[start] =
        '\0';

    size_t reconstructed_length =
        0;

    while (
        current != namespace_root &&
        current != visible_root
    ) {
        char component[
            VFS_NAME_MAX + 1U
        ];

        size_t component_length =
            0;

        struct vfs_node *parent =
            NULL;

        result =
            process_getcwd_parent_component(
                mounts,
                current,
                component,
                &component_length,
                &parent
            );

        if (
            result !=
            PROCESS_GETCWD_RESULT_SUCCESS
        ) {
            (void) vfs_node_release(
                current
            );

            (void) vfs_node_release(
                visible_root
            );

            return
                result;
        }

        size_t component_size =
            component_length + 1U;

        if (
            component_size >
                VFS_PATH_MAX -
                reconstructed_length
        ) {
            (void) vfs_node_release(
                parent
            );

            (void) vfs_node_release(
                current
            );

            (void) vfs_node_release(
                visible_root
            );

            return
                PROCESS_GETCWD_RESULT_PATH_TOO_LONG;
        }

        if (
            start <
                component_size
        ) {
            (void) vfs_node_release(
                parent
            );

            (void) vfs_node_release(
                current
            );

            (void) vfs_node_release(
                visible_root
            );

            return
                PROCESS_GETCWD_RESULT_BUFFER_TOO_SMALL;
        }

        start -=
            component_length;

        for (
            size_t index = 0;
            index < component_length;
            ++index
        ) {
            buffer[
                start + index
            ] =
                component[index];
        }

        --start;

        buffer[start] =
            '/';

        reconstructed_length +=
            component_size;

        if (!vfs_node_release(
            current
        )) {
            (void) vfs_node_release(
                parent
            );

            (void) vfs_node_release(
                visible_root
            );

            return
                PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
        }

        current =
            parent;
    }

    if (
        reconstructed_length == 0
    ) {
        if (start == 0) {
            (void) vfs_node_release(
                current
            );

            (void) vfs_node_release(
                visible_root
            );

            return
                PROCESS_GETCWD_RESULT_BUFFER_TOO_SMALL;
        }

        --start;

        buffer[start] =
            '/';

        reconstructed_length =
            1;
    }

    size_t output_size =
        reconstructed_length + 1U;

    /*
     * The destination begins before the source whenever start is non-zero.
     * Copying from low to high indices is therefore safe for this overlap.
     */
    for (
        size_t index = 0;
        index < output_size;
        ++index
    ) {
        buffer[index] =
            buffer[
                start + index
            ];
    }

    if (!vfs_node_release(
        current
    )) {
        (void) vfs_node_release(
            visible_root
        );

        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    if (!vfs_node_release(
        visible_root
    )) {
        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    *path_length =
        reconstructed_length;

    return
        PROCESS_GETCWD_RESULT_SUCCESS;
}

// Private functions and helpers implementations
static enum process_getcwd_result process_getcwd_visible_root(
    struct vfs_node *root,
    const struct vfs_mount_table *mounts,
    struct vfs_node **result)
{
    if (
        root == NULL ||
        result == NULL ||
        root->reference_count == 0 ||
        root->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    if (!vfs_node_retain(
        root
    )) {
        return
            PROCESS_GETCWD_RESULT_RESOURCE_EXHAUSTED;
    }

    if (mounts == NULL) {
        *result =
            root;

        return
            PROCESS_GETCWD_RESULT_SUCCESS;
    }

    struct vfs_node *mounted_root =
        NULL;

    enum vfs_mount_result mount_result =
        vfs_mount_table_lookup_mounted_root(
            mounts,
            root,
            &mounted_root
        );

    switch (mount_result) {
        case VFS_MOUNT_RESULT_NOT_FOUND:
            *result =
                root;

            return
                PROCESS_GETCWD_RESULT_SUCCESS;

        case VFS_MOUNT_RESULT_SUCCESS:
            break;

        case VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED:
            (void) vfs_node_release(
                root
            );

            return
                PROCESS_GETCWD_RESULT_RESOURCE_EXHAUSTED;

        case VFS_MOUNT_RESULT_INVALID_ARGUMENT:
        case VFS_MOUNT_RESULT_CONFLICT:
            (void) vfs_node_release(
                root
            );

            return
                PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    if (
        mounted_root == NULL ||
        !vfs_node_release(
            root
        )
    ) {
        if (
            mounted_root != NULL
        ) {
            (void) vfs_node_release(
                mounted_root
            );
        }

        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    *result =
        mounted_root;

    return
        PROCESS_GETCWD_RESULT_SUCCESS;
}

static enum process_getcwd_result process_getcwd_parent_component(
    const struct vfs_mount_table *mounts,
    struct vfs_node *current,
    char *name,
    size_t *name_length,
    struct vfs_node **parent)
{
    if (
        current == NULL ||
        name == NULL ||
        name_length == NULL ||
        parent == NULL ||
        current->reference_count == 0 ||
        current->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_node *mountpoint =
        NULL;

    struct vfs_node *component =
        current;

    if (mounts != NULL) {
        enum vfs_mount_result mount_result =
            vfs_mount_table_lookup_mountpoint(
                mounts,
                current,
                &mountpoint
            );

        switch (mount_result) {
            case VFS_MOUNT_RESULT_SUCCESS:
                component =
                    mountpoint;
                break;

            case VFS_MOUNT_RESULT_NOT_FOUND:
                break;

            case VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED:
                return
                    PROCESS_GETCWD_RESULT_RESOURCE_EXHAUSTED;

            case VFS_MOUNT_RESULT_INVALID_ARGUMENT:
            case VFS_MOUNT_RESULT_CONFLICT:
                return
                    PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
        }
    }

    struct vfs_node *resolved_parent =
        NULL;

    enum vfs_parent_result parent_result =
        vfs_node_parent(
            component,
            &resolved_parent
        );

    if (
        parent_result !=
        VFS_PARENT_RESULT_FOUND
    ) {
        if (
            mountpoint != NULL
        ) {
            (void) vfs_node_release(
                mountpoint
            );
        }

        switch (parent_result) {
            case VFS_PARENT_RESULT_NO_PARENT:
                return
                    PROCESS_GETCWD_RESULT_NOT_FOUND;

            case VFS_PARENT_RESULT_NOT_SUPPORTED:
                return
                    PROCESS_GETCWD_RESULT_NOT_SUPPORTED;

            case VFS_PARENT_RESULT_RESOURCE_EXHAUSTED:
                return
                    PROCESS_GETCWD_RESULT_RESOURCE_EXHAUSTED;

            case VFS_PARENT_RESULT_INVALID_ARGUMENT:
            case VFS_PARENT_RESULT_NOT_DIRECTORY:
                return
                    PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;

            case VFS_PARENT_RESULT_FOUND:
                break;
        }

        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    if (
        resolved_parent == NULL ||
        resolved_parent == current ||
        resolved_parent == component
    ) {
        if (
            resolved_parent != NULL
        ) {
            (void) vfs_node_release(
                resolved_parent
            );
        }

        if (
            mountpoint != NULL
        ) {
            (void) vfs_node_release(
                mountpoint
            );
        }

        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    enum process_getcwd_result result =
        process_getcwd_find_child_name(
            resolved_parent,
            component,
            name,
            name_length
        );

    if (
        mountpoint != NULL &&
        !vfs_node_release(
            mountpoint
        )
    ) {
        (void) vfs_node_release(
            resolved_parent
        );

        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    if (
        result !=
        PROCESS_GETCWD_RESULT_SUCCESS
    ) {
        (void) vfs_node_release(
            resolved_parent
        );

        return
            result;
    }

    *parent =
        resolved_parent;

    return
        PROCESS_GETCWD_RESULT_SUCCESS;
}

static enum process_getcwd_result process_getcwd_find_child_name(
    struct vfs_node *parent,
    struct vfs_node *child,
    char *name,
    size_t *name_length)
{
    if (
        parent == NULL ||
        child == NULL ||
        name == NULL ||
        name_length == NULL ||
        parent->reference_count == 0 ||
        child->reference_count == 0 ||
        parent->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_file *directory =
        NULL;

    enum vfs_open_result open_result =
        vfs_node_open(
            parent,
            VFS_OPEN_ACCESS_READ,
            &directory
        );

    if (
        open_result !=
        VFS_OPEN_RESULT_OPENED
    ) {
        switch (open_result) {
            case VFS_OPEN_RESULT_NOT_SUPPORTED:
                return
                    PROCESS_GETCWD_RESULT_NOT_SUPPORTED;

            case VFS_OPEN_RESULT_ACCESS_DENIED:
                return
                    PROCESS_GETCWD_RESULT_ACCESS_DENIED;

            case VFS_OPEN_RESULT_RESOURCE_EXHAUSTED:
                return
                    PROCESS_GETCWD_RESULT_RESOURCE_EXHAUSTED;

            case VFS_OPEN_RESULT_INVALID_ARGUMENT:
                return
                    PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;

            case VFS_OPEN_RESULT_OPENED:
                break;
        }

        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    enum process_getcwd_result result =
        PROCESS_GETCWD_RESULT_NOT_FOUND;

    for (;;) {
        struct vfs_directory_entry entry;

        enum vfs_directory_read_result read_result =
            vfs_file_read_directory(
                directory,
                &entry
            );

        if (
            read_result ==
            VFS_DIRECTORY_READ_RESULT_END
        ) {
            result =
                PROCESS_GETCWD_RESULT_NOT_FOUND;

            break;
        }

        if (
            read_result !=
            VFS_DIRECTORY_READ_RESULT_ENTRY
        ) {
            switch (read_result) {
                case VFS_DIRECTORY_READ_RESULT_NOT_SUPPORTED:
                    result =
                        PROCESS_GETCWD_RESULT_NOT_SUPPORTED;
                    break;

                case VFS_DIRECTORY_READ_RESULT_ACCESS_DENIED:
                    result =
                        PROCESS_GETCWD_RESULT_ACCESS_DENIED;
                    break;

                case VFS_DIRECTORY_READ_RESULT_INVALIDATED:
                    result =
                        PROCESS_GETCWD_RESULT_INVALIDATED;
                    break;

                case VFS_DIRECTORY_READ_RESULT_RESOURCE_EXHAUSTED:
                    result =
                        PROCESS_GETCWD_RESULT_RESOURCE_EXHAUSTED;
                    break;

                case VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT:
                case VFS_DIRECTORY_READ_RESULT_NOT_DIRECTORY:
                    result =
                        PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
                    break;

                case VFS_DIRECTORY_READ_RESULT_ENTRY:
                case VFS_DIRECTORY_READ_RESULT_END:
                    break;
            }

            break;
        }

        struct vfs_node *candidate =
            NULL;

        enum vfs_lookup_result lookup_result =
            vfs_node_lookup(
                parent,
                entry.name,
                entry.name_length,
                &candidate
            );

        if (
            lookup_result !=
            VFS_LOOKUP_RESULT_FOUND
        ) {
            switch (lookup_result) {
                case VFS_LOOKUP_RESULT_NOT_FOUND:
                    result =
                        PROCESS_GETCWD_RESULT_INVALIDATED;
                    break;

                case VFS_LOOKUP_RESULT_NOT_SUPPORTED:
                    result =
                        PROCESS_GETCWD_RESULT_NOT_SUPPORTED;
                    break;

                case VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED:
                    result =
                        PROCESS_GETCWD_RESULT_RESOURCE_EXHAUSTED;
                    break;

                case VFS_LOOKUP_RESULT_INVALID_ARGUMENT:
                case VFS_LOOKUP_RESULT_NOT_DIRECTORY:
                    result =
                        PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
                    break;

                case VFS_LOOKUP_RESULT_FOUND:
                    break;
            }

            break;
        }

        bool matches =
            candidate == child;

        if (!vfs_node_release(
            candidate
        )) {
            result =
                PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;

            break;
        }

        if (!matches) {
            continue;
        }

        for (
            size_t index = 0;
            index < entry.name_length;
            ++index
        ) {
            name[index] =
                entry.name[index];
        }

        name[
            entry.name_length
        ] =
            '\0';

        *name_length =
            entry.name_length;

        result =
            PROCESS_GETCWD_RESULT_SUCCESS;

        break;
    }

    if (!vfs_file_release(
        directory
    )) {
        return
            PROCESS_GETCWD_RESULT_INVALID_ARGUMENT;
    }

    return
        result;
}

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

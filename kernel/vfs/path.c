// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file path.c
 * @brief Generic VFS pathname resolution implementation.
 */

#include "path.h"

#include <stdbool.h>
#include <stddef.h>

// Private functions and helpers declarations
static bool vfs_path_input_valid(
    const char *path,
    size_t path_length
);

static bool vfs_path_component_is_dot(
    const char *component,
    size_t component_length
);

static bool vfs_path_component_is_dot_dot(
    const char *component,
    size_t component_length
);

static enum vfs_path_result vfs_path_lookup_result(
    enum vfs_lookup_result result
);

static enum vfs_path_result vfs_path_parent_result(
    enum vfs_parent_result result
);

static enum vfs_path_result vfs_path_mount_result(
    enum vfs_mount_result result
);

static enum vfs_path_result vfs_path_follow_mount(
    const struct vfs_mount_table *mounts,
    struct vfs_node **node
);

static enum vfs_path_result vfs_path_parent_step(
    const struct vfs_mount_table *mounts,
    struct vfs_node *namespace_root,
    struct vfs_node **current
);

static enum vfs_path_result vfs_path_replace_current(
    struct vfs_node **current,
    struct vfs_node *next
);

static enum vfs_path_result vfs_path_fail(
    struct vfs_node *current,
    struct vfs_node *namespace_root,
    enum vfs_path_result result
);

// Public functions implementations
enum vfs_path_result vfs_path_resolve(
    struct vfs_node *root,
    struct vfs_node *start,
    const struct vfs_mount_table *mounts,
    const char *path,
    size_t path_length,
    struct vfs_node **result)
{
    if (
        root == NULL ||
        start == NULL ||
        result == NULL
    ) {
        return
            VFS_PATH_RESULT_INVALID_ARGUMENT;
    }

    if (
        root->reference_count == 0 ||
        start->reference_count == 0 ||
        root->type != VFS_NODE_TYPE_DIRECTORY ||
        start->type != VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            VFS_PATH_RESULT_INVALID_ARGUMENT;
    }

    if (!vfs_path_input_valid(
        path,
        path_length
    )) {
        return
            VFS_PATH_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Hold an independent reference to the visible namespace root for the
     * complete traversal. This makes namespace-boundary checks independent of
     * movement of the current node.
     */
    struct vfs_node *namespace_root =
        root;

    if (!vfs_node_retain(
        namespace_root
    )) {
        return
            VFS_PATH_RESULT_RESOURCE_EXHAUSTED;
    }

    enum vfs_path_result follow_result =
        vfs_path_follow_mount(
            mounts,
            &namespace_root
        );

    if (
        follow_result !=
        VFS_PATH_RESULT_FOUND
    ) {
        (void) vfs_node_release(
            namespace_root
        );

        return
            follow_result;
    }

    struct vfs_node *current;

    if (path[0] == '/') {
        current =
            namespace_root;

        if (!vfs_node_retain(
            current
        )) {
            (void) vfs_node_release(
                namespace_root
            );

            return
                VFS_PATH_RESULT_RESOURCE_EXHAUSTED;
        }
    } else {
        current =
            start;

        if (!vfs_node_retain(
            current
        )) {
            (void) vfs_node_release(
                namespace_root
            );

            return
                VFS_PATH_RESULT_RESOURCE_EXHAUSTED;
        }

        follow_result =
            vfs_path_follow_mount(
                mounts,
                &current
            );

        if (
            follow_result !=
            VFS_PATH_RESULT_FOUND
        ) {
            return vfs_path_fail(
                current,
                namespace_root,
                follow_result
            );
        }
    }

    size_t index =
        0;

    while (index < path_length) {
        while (
            index < path_length &&
            path[index] == '/'
        ) {
            ++index;
        }

        if (index == path_length) {
            break;
        }

        size_t component_start =
            index;

        while (
            index < path_length &&
            path[index] != '/'
        ) {
            ++index;
        }

        size_t component_length =
            index - component_start;

        if (
            component_length == 0 ||
            component_length > VFS_NAME_MAX
        ) {
            return vfs_path_fail(
                current,
                namespace_root,
                VFS_PATH_RESULT_INVALID_ARGUMENT
            );
        }

        const char *component =
            path + component_start;

        if (vfs_path_component_is_dot(
            component,
            component_length
        )) {
            if (
                current->type !=
                VFS_NODE_TYPE_DIRECTORY
            ) {
                return vfs_path_fail(
                    current,
                    namespace_root,
                    VFS_PATH_RESULT_NOT_DIRECTORY
                );
            }

            continue;
        }

        if (vfs_path_component_is_dot_dot(
            component,
            component_length
        )) {
            if (
                current->type !=
                VFS_NODE_TYPE_DIRECTORY
            ) {
                return vfs_path_fail(
                    current,
                    namespace_root,
                    VFS_PATH_RESULT_NOT_DIRECTORY
                );
            }

            enum vfs_path_result parent_result =
                vfs_path_parent_step(
                    mounts,
                    namespace_root,
                    &current
                );

            if (
                parent_result !=
                VFS_PATH_RESULT_FOUND
            ) {
                return vfs_path_fail(
                    current,
                    namespace_root,
                    parent_result
                );
            }

            continue;
        }

        struct vfs_node *child =
            NULL;

        enum vfs_lookup_result lookup_result =
            vfs_node_lookup(
                current,
                component,
                component_length,
                &child
            );

        if (
            lookup_result !=
            VFS_LOOKUP_RESULT_FOUND
        ) {
            return vfs_path_fail(
                current,
                namespace_root,
                vfs_path_lookup_result(
                    lookup_result
                )
            );
        }

        follow_result =
            vfs_path_follow_mount(
                mounts,
                &child
            );

        if (
            follow_result !=
            VFS_PATH_RESULT_FOUND
        ) {
            (void) vfs_node_release(
                child
            );

            return vfs_path_fail(
                current,
                namespace_root,
                follow_result
            );
        }

        enum vfs_path_result replace_result =
            vfs_path_replace_current(
                &current,
                child
            );

        if (
            replace_result !=
            VFS_PATH_RESULT_FOUND
        ) {
            (void) vfs_node_release(
                namespace_root
            );

            return
                replace_result;
        }
    }

    /*
     * A trailing slash carries directory semantics even though repeated
     * separators themselves do not create components.
     */
    if (
        path[path_length - 1] == '/' &&
        current->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return vfs_path_fail(
            current,
            namespace_root,
            VFS_PATH_RESULT_NOT_DIRECTORY
        );
    }

    /*
     * current carries the result ownership. The independent namespace-root
     * reference is no longer needed once traversal has completed.
     */
    if (!vfs_node_release(
        namespace_root
    )) {
        (void) vfs_node_release(
            current
        );

        return
            VFS_PATH_RESULT_INVALID_ARGUMENT;
    }

    *result =
        current;

    return
        VFS_PATH_RESULT_FOUND;
}

// Private functions and helpers implementations
static bool vfs_path_input_valid(
    const char *path,
    size_t path_length)
{
    if (
        path == NULL ||
        path_length == 0 ||
        path_length > VFS_PATH_MAX
    ) {
        return false;
    }

    for (size_t index = 0;
         index < path_length;
         ++index) {
        if (path[index] == '\0') {
            return false;
        }
    }

    return true;
}

static bool vfs_path_component_is_dot(
    const char *component,
    size_t component_length)
{
    return
        component_length == 1 &&
        component[0] == '.';
}

static bool vfs_path_component_is_dot_dot(
    const char *component,
    size_t component_length)
{
    return
        component_length == 2 &&
        component[0] == '.' &&
        component[1] == '.';
}

static enum vfs_path_result vfs_path_lookup_result(
    enum vfs_lookup_result result)
{
    switch (result) {
        case VFS_LOOKUP_RESULT_NOT_FOUND:
            return
                VFS_PATH_RESULT_NOT_FOUND;

        case VFS_LOOKUP_RESULT_INVALID_ARGUMENT:
            return
                VFS_PATH_RESULT_INVALID_ARGUMENT;

        case VFS_LOOKUP_RESULT_NOT_DIRECTORY:
            return
                VFS_PATH_RESULT_NOT_DIRECTORY;

        case VFS_LOOKUP_RESULT_NOT_SUPPORTED:
            return
                VFS_PATH_RESULT_NOT_SUPPORTED;

        case VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED:
            return
                VFS_PATH_RESULT_RESOURCE_EXHAUSTED;

        case VFS_LOOKUP_RESULT_FOUND:
            break;
    }

    return
        VFS_PATH_RESULT_INVALID_ARGUMENT;
}

static enum vfs_path_result vfs_path_parent_result(
    enum vfs_parent_result result)
{
    switch (result) {
        case VFS_PARENT_RESULT_NO_PARENT:
            return
                VFS_PATH_RESULT_NOT_FOUND;

        case VFS_PARENT_RESULT_INVALID_ARGUMENT:
            return
                VFS_PATH_RESULT_INVALID_ARGUMENT;

        case VFS_PARENT_RESULT_NOT_DIRECTORY:
            return
                VFS_PATH_RESULT_NOT_DIRECTORY;

        case VFS_PARENT_RESULT_NOT_SUPPORTED:
            return
                VFS_PATH_RESULT_NOT_SUPPORTED;

        case VFS_PARENT_RESULT_RESOURCE_EXHAUSTED:
            return
                VFS_PATH_RESULT_RESOURCE_EXHAUSTED;

        case VFS_PARENT_RESULT_FOUND:
            break;
    }

    return
        VFS_PATH_RESULT_INVALID_ARGUMENT;
}

static enum vfs_path_result vfs_path_mount_result(
    enum vfs_mount_result result)
{
    switch (result) {
        case VFS_MOUNT_RESULT_NOT_FOUND:
            return
                VFS_PATH_RESULT_NOT_FOUND;

        case VFS_MOUNT_RESULT_INVALID_ARGUMENT:
        case VFS_MOUNT_RESULT_CONFLICT:
            return
                VFS_PATH_RESULT_INVALID_ARGUMENT;

        case VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED:
            return
                VFS_PATH_RESULT_RESOURCE_EXHAUSTED;

        case VFS_MOUNT_RESULT_SUCCESS:
            break;
    }

    return
        VFS_PATH_RESULT_INVALID_ARGUMENT;
}

static enum vfs_path_result vfs_path_follow_mount(
    const struct vfs_mount_table *mounts,
    struct vfs_node **node)
{
    if (
        node == NULL ||
        *node == NULL ||
        (*node)->reference_count == 0
    ) {
        return
            VFS_PATH_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Only directories can participate as mountpoints. Avoid presenting
     * ordinary files to the stricter mount-table lookup contract.
     */
    if (
        mounts == NULL ||
        (*node)->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            VFS_PATH_RESULT_FOUND;
    }

    struct vfs_node *mounted_root =
        NULL;

    enum vfs_mount_result mount_result =
        vfs_mount_table_lookup_mounted_root(
            mounts,
            *node,
            &mounted_root
        );

    if (
        mount_result ==
        VFS_MOUNT_RESULT_NOT_FOUND
    ) {
        return
            VFS_PATH_RESULT_FOUND;
    }

    if (
        mount_result !=
        VFS_MOUNT_RESULT_SUCCESS
    ) {
        return
            vfs_path_mount_result(
                mount_result
            );
    }

    /*
     * mounted_root is already an owned reference. Replace the covered node
     * without changing the traversal's total ownership.
     */
    if (!vfs_node_release(
        *node
    )) {
        (void) vfs_node_release(
            mounted_root
        );

        return
            VFS_PATH_RESULT_INVALID_ARGUMENT;
    }

    *node =
        mounted_root;

    return
        VFS_PATH_RESULT_FOUND;
}

static enum vfs_path_result vfs_path_parent_step(
    const struct vfs_mount_table *mounts,
    struct vfs_node *namespace_root,
    struct vfs_node **current)
{
    if (
        namespace_root == NULL ||
        current == NULL ||
        *current == NULL
    ) {
        return
            VFS_PATH_RESULT_INVALID_ARGUMENT;
    }

    /*
     * The namespace boundary takes precedence over mount traversal. A mounted
     * root used as the namespace root therefore cannot escape through "..".
     */
    if (*current == namespace_root) {
        return
            VFS_PATH_RESULT_FOUND;
    }

    struct vfs_node *parent =
        NULL;

    if (mounts != NULL) {
        struct vfs_node *mountpoint =
            NULL;

        enum vfs_mount_result mount_result =
            vfs_mount_table_lookup_mountpoint(
                mounts,
                *current,
                &mountpoint
            );

        if (
            mount_result ==
            VFS_MOUNT_RESULT_SUCCESS
        ) {
            enum vfs_parent_result parent_result =
                vfs_node_parent(
                    mountpoint,
                    &parent
                );

            if (!vfs_node_release(
                mountpoint
            )) {
                if (parent != NULL) {
                    (void) vfs_node_release(
                        parent
                    );
                }

                return
                    VFS_PATH_RESULT_INVALID_ARGUMENT;
            }

            if (
                parent_result !=
                VFS_PARENT_RESULT_FOUND
            ) {
                return
                    vfs_path_parent_result(
                        parent_result
                    );
            }

            enum vfs_path_result follow_result =
                vfs_path_follow_mount(
                    mounts,
                    &parent
                );

            if (
                follow_result !=
                VFS_PATH_RESULT_FOUND
            ) {
                (void) vfs_node_release(
                    parent
                );

                return
                    follow_result;
            }

            return
                vfs_path_replace_current(
                    current,
                    parent
                );
        }

        if (
            mount_result !=
            VFS_MOUNT_RESULT_NOT_FOUND
        ) {
            return
                vfs_path_mount_result(
                    mount_result
                );
        }
    }

    enum vfs_parent_result parent_result =
        vfs_node_parent(
            *current,
            &parent
        );

    if (
        parent_result !=
        VFS_PARENT_RESULT_FOUND
    ) {
        return
            vfs_path_parent_result(
                parent_result
            );
    }

    enum vfs_path_result follow_result =
        vfs_path_follow_mount(
            mounts,
            &parent
        );

    if (
        follow_result !=
        VFS_PATH_RESULT_FOUND
    ) {
        (void) vfs_node_release(
            parent
        );

        return
            follow_result;
    }

    return
        vfs_path_replace_current(
            current,
            parent
        );
}

static enum vfs_path_result vfs_path_replace_current(
    struct vfs_node **current,
    struct vfs_node *next)
{
    if (
        current == NULL ||
        *current == NULL ||
        next == NULL
    ) {
        if (next != NULL) {
            (void) vfs_node_release(
                next
            );
        }

        return
            VFS_PATH_RESULT_INVALID_ARGUMENT;
    }

    if (!vfs_node_release(
        *current
    )) {
        /*
         * next already belongs to this traversal. Do not leak it if the
         * supposedly owned current reference is inconsistent.
         */
        (void) vfs_node_release(
            next
        );

        return
            VFS_PATH_RESULT_INVALID_ARGUMENT;
    }

    *current =
        next;

    return
        VFS_PATH_RESULT_FOUND;
}

static enum vfs_path_result vfs_path_fail(
    struct vfs_node *current,
    struct vfs_node *namespace_root,
    enum vfs_path_result result)
{
    if (current != NULL) {
        (void) vfs_node_release(
            current
        );
    }

    if (namespace_root != NULL) {
        (void) vfs_node_release(
            namespace_root
        );
    }

    return
        result;
}

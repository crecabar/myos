// SPDX-License-Identifier: GPL-2.0-only

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

static enum vfs_path_result vfs_path_replace_current(
    struct vfs_node **current,
    struct vfs_node *next
);

static enum vfs_path_result vfs_path_fail(
    struct vfs_node *current,
    enum vfs_path_result result
);

// Public functions implementations
enum vfs_path_result vfs_path_resolve(
    struct vfs_node *root,
    struct vfs_node *start,
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

    struct vfs_node *current =
        path[0] == '/'
            ? root
            : start;

    if (!vfs_node_retain(
        current
    )) {
        return
            VFS_PATH_RESULT_RESOURCE_EXHAUSTED;
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
                    VFS_PATH_RESULT_NOT_DIRECTORY
                );
            }

            /*
             * Namespace traversal is bounded at the supplied root. The
             * filesystem does not need to manufacture a self-parent link for
             * its root node.
             */
            if (current == root) {
                continue;
            }

            struct vfs_node *parent =
                NULL;

            enum vfs_parent_result parent_result =
                vfs_node_parent(
                    current,
                    &parent
                );

            if (
                parent_result !=
                VFS_PARENT_RESULT_FOUND
            ) {
                return vfs_path_fail(
                    current,
                    vfs_path_parent_result(
                        parent_result
                    )
                );
            }

            enum vfs_path_result replace_result =
                vfs_path_replace_current(
                    &current,
                    parent
                );

            if (
                replace_result !=
                VFS_PATH_RESULT_FOUND
            ) {
                return
                    replace_result;
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
                vfs_path_lookup_result(
                    lookup_result
                )
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
            VFS_PATH_RESULT_NOT_DIRECTORY
        );
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
    enum vfs_path_result result)
{
    if (current != NULL) {
        (void) vfs_node_release(
            current
        );
    }

    return
        result;
}

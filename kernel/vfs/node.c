// SPDX-License-Identifier: GPL-2.0-only

#include "vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static bool vfs_node_type_valid(
    enum vfs_node_type type
);

static bool vfs_lookup_name_valid(
    const char *name,
    size_t name_length
);

// Public functions implementations
bool vfs_node_initialize(
    struct vfs_node *node,
    enum vfs_node_type type,
    const struct vfs_node_operations *operations,
    void *private_data)
{
    if (node == NULL) {
        return false;
    }

    if (!vfs_node_type_valid(type)) {
        return false;
    }

    node->type =
        type;

    node->reference_count =
        1;

    node->operations =
        operations;

    node->private_data =
        private_data;

    return true;
}

bool vfs_node_retain(
    struct vfs_node *node)
{
    if (node == NULL) {
        return false;
    }

    if (
        node->reference_count == 0 ||
        node->reference_count == SIZE_MAX
    ) {
        return false;
    }

    ++node->reference_count;

    return true;
}

bool vfs_node_release(
    struct vfs_node *node)
{
    if (node == NULL) {
        return false;
    }

    if (node->reference_count == 0) {
        return false;
    }

    --node->reference_count;

    if (node->reference_count != 0) {
        return true;
    }

    const struct vfs_node_operations *operations =
        node->operations;

    if (
        operations != NULL &&
        operations->destroy != NULL
    ) {
        operations->destroy(
            node
        );
    }

    /*
     * Do not inspect node after destroy(). The filesystem is allowed to have
     * released the storage containing it.
     */
    return true;
}

enum vfs_lookup_result vfs_node_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result)
{
    if (
        directory == NULL ||
        name == NULL ||
        result == NULL
    ) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    if (directory->reference_count == 0) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    if (!vfs_lookup_name_valid(
        name,
        name_length
    )) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    if (
        directory->type !=
        VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            VFS_LOOKUP_RESULT_NOT_DIRECTORY;
    }

    if (
        directory->operations == NULL ||
        directory->operations->lookup == NULL
    ) {
        return
            VFS_LOOKUP_RESULT_NOT_SUPPORTED;
    }

    struct vfs_node *child =
        directory->operations->lookup(
            directory,
            name,
            name_length
        );

    if (child == NULL) {
        return
            VFS_LOOKUP_RESULT_NOT_FOUND;
    }

    /*
     * Filesystem lookup returns a borrowed child. The generic VFS turns that
     * into the owned reference exposed to its caller.
     */
    if (!vfs_node_retain(
        child
    )) {
        return
            VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED;
    }

    *result =
        child;

    return
        VFS_LOOKUP_RESULT_FOUND;
}

// Private functions and helpers implementations
static bool vfs_node_type_valid(
    enum vfs_node_type type)
{
    switch (type) {
        case VFS_NODE_TYPE_REGULAR_FILE:
        case VFS_NODE_TYPE_DIRECTORY:
        case VFS_NODE_TYPE_CHARACTER_DEVICE:
            return true;
    }

    return false;
}

static bool vfs_lookup_name_valid(
    const char *name,
    size_t name_length)
{
    if (
        name == NULL ||
        name_length == 0 ||
        name_length > VFS_NAME_MAX
    ) {
        return false;
    }

    for (size_t index = 0;
         index < name_length;
         ++index) {
        if (
            name[index] == '\0' ||
            name[index] == '/'
        ) {
            return false;
        }
    }

    if (
        name_length == 1 &&
        name[0] == '.'
    ) {
        return false;
    }

    if (
        name_length == 2 &&
        name[0] == '.' &&
        name[1] == '.'
    ) {
        return false;
    }

    return true;
}

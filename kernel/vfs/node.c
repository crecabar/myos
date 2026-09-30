// SPDX-License-Identifier: GPL-2.0-only

#include "vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static bool vfs_node_type_valid(
    enum vfs_node_type type
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

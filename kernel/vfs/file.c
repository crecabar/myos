// SPDX-License-Identifier: GPL-2.0-only

#include "vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Public functions implementations
bool vfs_file_initialize(
    struct vfs_file *file,
    struct vfs_node *node,
    const struct vfs_file_operations *operations,
    void *private_data)
{
    if (
        file == NULL ||
        node == NULL
    ) {
        return false;
    }

    if (!vfs_node_retain(
        node
    )) {
        return false;
    }

    file->reference_count =
        1;

    file->node =
        node;

    file->offset =
        0;

    file->operations =
        operations;

    file->private_data =
        private_data;

    return true;
}

bool vfs_file_retain(
    struct vfs_file *file)
{
    if (file == NULL) {
        return false;
    }

    if (
        file->reference_count == 0 ||
        file->reference_count == SIZE_MAX
    ) {
        return false;
    }

    ++file->reference_count;

    return true;
}

bool vfs_file_release(
    struct vfs_file *file)
{
    if (file == NULL) {
        return false;
    }

    if (file->reference_count == 0) {
        return false;
    }

    --file->reference_count;

    if (file->reference_count != 0) {
        return true;
    }

    struct vfs_node *node =
        file->node;

    const struct vfs_file_operations *operations =
        file->operations;

    if (
        operations != NULL &&
        operations->destroy != NULL
    ) {
        operations->destroy(
            file
        );
    }

    /*
     * destroy() may release the storage containing file. Only the retained
     * node pointer captured above remains valid to use here.
     */
    return vfs_node_release(
        node
    );
}

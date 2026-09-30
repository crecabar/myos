// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file cwd.c
 * @brief Per-process current working directory ownership implementation.
 */

#include "cwd.h"

#include <stddef.h>

struct vfs_node *process_cwd_get(
    const struct process_instance *instance)
{
    if (instance == NULL) {
        return NULL;
    }

    return
        instance->current_directory;
}

bool process_cwd_set(
    struct process_instance *instance,
    struct vfs_node *directory)
{
    if (
        instance == NULL ||
        directory == NULL
    ) {
        return false;
    }

    if (
        directory->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        directory->reference_count == 0
    ) {
        return false;
    }

    struct vfs_node *previous =
        instance->current_directory;

    if (previous == directory) {
        return true;
    }

    /*
     * Validate existing ownership before acquiring anything new so failures
     * leave the instance unchanged.
     */
    if (
        previous != NULL &&
        (
            previous->type !=
                VFS_NODE_TYPE_DIRECTORY ||
            previous->reference_count == 0
        )
    ) {
        return false;
    }

    /*
     * Retain before publication. The old CWD therefore remains valid if this
     * acquisition fails.
     */
    if (!vfs_node_retain(
        directory
    )) {
        return false;
    }

    instance->current_directory =
        directory;

    if (
        previous != NULL &&
        !vfs_node_release(
            previous
        )
    ) {
        /*
         * vfs_node_release() leaves a dead reference unchanged when it fails.
         * Restore the previous ownership and release the reference acquired
         * above.
         */
        instance->current_directory =
            previous;

        (void) vfs_node_release(
            directory
        );

        return false;
    }

    return true;
}

bool process_cwd_inherit(
    struct process_instance *destination,
    const struct process_instance *source)
{
    if (
        destination == NULL ||
        source == NULL ||
        destination == source
    ) {
        return false;
    }

    if (
        destination->current_directory !=
        NULL
    ) {
        return false;
    }

    struct vfs_node *directory =
        source->current_directory;

    if (directory == NULL) {
        return true;
    }

    if (
        directory->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        directory->reference_count == 0
    ) {
        return false;
    }

    if (!vfs_node_retain(
        directory
    )) {
        return false;
    }

    destination->current_directory =
        directory;

    return true;
}

bool process_cwd_release(
    struct process_instance *instance)
{
    if (instance == NULL) {
        return false;
    }

    struct vfs_node *directory =
        instance->current_directory;

    if (directory == NULL) {
        return true;
    }

    if (
        directory->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        directory->reference_count == 0
    ) {
        return false;
    }

    /*
     * Unpublish before release because the final reference may destroy the
     * node storage.
     */
    instance->current_directory =
        NULL;

    if (!vfs_node_release(
        directory
    )) {
        /*
         * A failed release does not consume the reference, so restoring the
         * pointer preserves the previous ownership state.
         */
        instance->current_directory =
            directory;

        return false;
    }

    return true;
}

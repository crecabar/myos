// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file namespace.c
 * @brief Per-process VFS namespace-root ownership implementation.
 */

#include "namespace.h"

#include <stddef.h>

struct vfs_node *process_namespace_root_get(
    const struct process_instance *instance)
{
    if (instance == NULL) {
        return NULL;
    }

    return
        instance->namespace_root;
}

const struct vfs_mount_table *process_namespace_mounts_get(
    const struct process_instance *instance)
{
    if (instance == NULL) {
        return NULL;
    }

    return
        instance->namespace_mounts;
}

bool process_namespace_root_set(
    struct process_instance *instance,
    struct vfs_node *root,
    const struct vfs_mount_table *mounts)
{
    if (
        instance == NULL ||
        root == NULL
    ) {
        return false;
    }

    if (
        instance->namespace_root != NULL ||
        instance->namespace_mounts != NULL
    ) {
        return false;
    }

    if (
        root->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        root->reference_count == 0
    ) {
        return false;
    }

    if (!vfs_node_retain(
        root
    )) {
        return false;
    }

    instance->namespace_root =
        root;

    instance->namespace_mounts =
        mounts;

    return true;
}

bool process_namespace_root_inherit(
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
        destination->namespace_root != NULL ||
        destination->namespace_mounts != NULL
    ) {
        return false;
    }

    struct vfs_node *root =
        source->namespace_root;

    const struct vfs_mount_table *mounts =
        source->namespace_mounts;

    if (root == NULL) {
        return
            mounts == NULL;
    }

    if (
        root->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        root->reference_count == 0
    ) {
        return false;
    }

    if (!vfs_node_retain(
        root
    )) {
        return false;
    }

    destination->namespace_root =
        root;

    destination->namespace_mounts =
        mounts;

    return true;
}

bool process_namespace_root_release(
    struct process_instance *instance)
{
    if (instance == NULL) {
        return false;
    }

    struct vfs_node *root =
        instance->namespace_root;

    const struct vfs_mount_table *mounts =
        instance->namespace_mounts;

    if (root == NULL) {
        return
            mounts == NULL;
    }

    if (
        root->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        root->reference_count == 0
    ) {
        return false;
    }

    /*
     * Unpublish the complete namespace context before releasing the owned root
     * reference. The mount table is borrowed and therefore requires no
     * release operation.
     */
    instance->namespace_root =
        NULL;

    instance->namespace_mounts =
        NULL;

    if (!vfs_node_release(
        root
    )) {
        /*
         * A failed release consumes no ownership, so restore the complete
         * namespace context.
         */
        instance->namespace_root =
            root;

        instance->namespace_mounts =
            mounts;

        return false;
    }

    return true;
}

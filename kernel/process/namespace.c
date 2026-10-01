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

bool process_namespace_root_set(
    struct process_instance *instance,
    struct vfs_node *root)
{
    if (
        instance == NULL ||
        root == NULL
    ) {
        return false;
    }

    if (instance->namespace_root != NULL) {
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

    if (destination->namespace_root != NULL) {
        return false;
    }

    struct vfs_node *root =
        source->namespace_root;

    if (root == NULL) {
        return true;
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

    if (root == NULL) {
        return true;
    }

    if (
        root->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        root->reference_count == 0
    ) {
        return false;
    }

    /*
     * Unpublish before release because the final reference may destroy the
     * storage containing the node.
     */
    instance->namespace_root =
        NULL;

    if (!vfs_node_release(
        root
    )) {
        /*
         * A failed release does not consume ownership, so restore the
         * previously published state.
         */
        instance->namespace_root =
            root;

        return false;
    }

    return true;
}

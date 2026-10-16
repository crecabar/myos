// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file root.c
 * @brief System VFS root publication implementation.
 */

#include "root.h"

#include <stddef.h>

// Static local variables
static struct vfs_node *vfs_system_root;

// Public functions implementations
bool vfs_root_install(
    struct vfs_node *root)
{
    if (
        root == NULL ||
        vfs_system_root != NULL
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

    vfs_system_root =
        root;

    return true;
}

struct vfs_node *vfs_root_get(void)
{
    return
        vfs_system_root;
}

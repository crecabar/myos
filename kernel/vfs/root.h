// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file root.h
 * @brief System VFS root publication.
 *
 * The system root is the directory from which the initial userspace namespace
 * will be derived. Filesystem-specific mounting remains outside this module.
 */

#ifndef MYOS_VFS_ROOT_H
#define MYOS_VFS_ROOT_H

#include "vfs.h"

#include <stdbool.h>

/**
 * Installs the initial system VFS root.
 *
 * Installation is one-shot. root must be a live directory and no root may
 * already be installed.
 *
 * On success, the VFS acquires one independent reference to root. The
 * filesystem retaining the node keeps its own ownership separately.
 *
 * @param root Live directory to publish as the system root.
 *
 * @return true when root was installed; false otherwise.
 */
bool vfs_root_install(
    struct vfs_node *root
);

/**
 * Returns the installed system VFS root.
 *
 * The returned pointer is borrowed. No reference count is changed.
 *
 * @return Borrowed root node, or NULL when no root has been installed.
 */
struct vfs_node *vfs_root_get(void);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file userspace.h
 * @brief Initial userspace bootstrap.
 */

#ifndef MYOS_INIT_USERSPACE_H
#define MYOS_INIT_USERSPACE_H

#include "../vfs/mount.h"
#include "../vfs/vfs.h"

#include <stdbool.h>

/**
 * Creates and publishes the initial userspace /init process.
 *
 * root is borrowed as the initial userspace namespace root. The created
 * process acquires independent references to root as both its namespace root
 * and initial current working directory.
 *
 * mounts is borrowed for the complete lifetime of the initial userspace
 * namespace and must therefore outlive /init.
 *
 * The /init executable is resolved from root, materialized completely into the
 * new process image, and then its temporary kernel-private executable backing
 * file is released before this function returns.
 *
 * This operation is one-shot. A second successful /init publication is not
 * permitted.
 *
 * @param root Borrowed system VFS root.
 * @param mounts Borrowed system mount topology, or NULL.
 *
 * @return true when /init was created and published to the scheduler; false
 * otherwise.
 */
bool userspace_init_start(
    struct vfs_node *root,
    const struct vfs_mount_table *mounts
);

#endif

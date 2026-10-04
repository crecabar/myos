// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file devfs.h
 * @brief Dynamic VFS projection of registered kernel devices.
 *
 * devfs exposes eligible devices from one generic device registry as ordinary
 * VFS nodes.
 *
 * Device existence and pathname publication remain separate concerns. Drivers
 * and generic devices do not create VFS paths and do not know where devfs is
 * mounted.
 *
 * The initial pathname policy maps one eligible device's stable internal name
 * directly to one component in the devfs root directory. This mapping belongs
 * to devfs and is not part of the generic device contract.
 *
 * The initial implementation publishes character devices only. Block-device
 * VFS adaptation is deliberately deferred until the corresponding VFS
 * contract exists.
 */

#ifndef MYOS_FS_DEVFS_H
#define MYOS_FS_DEVFS_H

#include "../core/device/registry.h"
#include "../vfs/vfs.h"

#include <stdbool.h>

struct devfs;

enum devfs_mount_result {
    DEVFS_MOUNT_RESULT_MOUNTED,
    DEVFS_MOUNT_RESULT_INVALID_ARGUMENT,
    DEVFS_MOUNT_RESULT_RESOURCE_EXHAUSTED,
};

/**
 * Creates one devfs view over an initialized device registry.
 *
 * registry is borrowed and must outlive the filesystem.
 *
 * The filesystem does not snapshot the registry. Successful registry
 * publication changes therefore become visible to subsequent devfs lookups.
 *
 * On success, result receives one owned filesystem object.
 * Failure leaves result unchanged.
 */
enum devfs_mount_result devfs_mount(
    const struct device_registry *registry,
    struct devfs **result
);

/**
 * Returns the devfs root directory.
 *
 * The returned pointer is borrowed. The filesystem owns its initial VFS
 * reference for the lifetime of the mounted devfs.
 */
struct vfs_node *devfs_root(
    struct devfs *filesystem
);

/**
 * Releases one devfs filesystem.
 *
 * Unmount succeeds only when the root and every materialized device vnode are
 * held solely by devfs itself. Outstanding lookup or open-file references
 * reject teardown.
 */
bool devfs_unmount(
    struct devfs *filesystem
);

#endif

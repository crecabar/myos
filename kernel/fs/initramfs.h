// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file initramfs.h
 * @brief Read-only initramfs filesystem backed by a CPIO newc archive.
 *
 * The filesystem borrows the archive byte range for its entire lifetime.
 * Directory and regular-file VFS nodes are materialized in kernel-owned heap
 * storage while pathname and file-data bytes continue to reference the
 * immutable archive.
 */

#ifndef MYOS_FS_INITRAMFS_H
#define MYOS_FS_INITRAMFS_H

#include "../vfs/vfs.h"

#include <stdbool.h>
#include <stddef.h>

struct initramfs;

enum initramfs_mount_result {
    INITRAMFS_MOUNT_RESULT_MOUNTED,
    INITRAMFS_MOUNT_RESULT_INVALID_ARGUMENT,
    INITRAMFS_MOUNT_RESULT_MALFORMED_ARCHIVE,
    INITRAMFS_MOUNT_RESULT_UNSUPPORTED_ARCHIVE,
    INITRAMFS_MOUNT_RESULT_RESOURCE_EXHAUSTED,
};

enum initramfs_namespace_result {
    INITRAMFS_NAMESPACE_RESULT_SUCCESS,
    INITRAMFS_NAMESPACE_RESULT_INVALID_ARGUMENT,
    INITRAMFS_NAMESPACE_RESULT_NOT_DIRECTORY,
    INITRAMFS_NAMESPACE_RESULT_ALREADY_EXISTS,
    INITRAMFS_NAMESPACE_RESULT_NOT_SUPPORTED,
    INITRAMFS_NAMESPACE_RESULT_RESOURCE_EXHAUSTED,
};

/**
 * Materializes one read-only initramfs filesystem.
 *
 * The archive is parsed completely before the filesystem is published.
 * Pathnames may appear in any archive order; parent relationships are resolved
 * only after all ordinary entries have been materialized.
 *
 * The filesystem borrows archive contents and therefore requires the archive
 * byte range to outlive the mounted filesystem.
 *
 * On success, result receives one owned filesystem object.
 * On failure, result remains unchanged and all temporary heap allocations are
 * released.
 *
 * @param archive CPIO newc archive bytes.
 * @param archive_size Number of archive bytes.
 * @param result Receives the mounted filesystem.
 *
 * @return Detailed mount result.
 */
enum initramfs_mount_result initramfs_mount(
    const void *archive,
    size_t archive_size,
    struct initramfs **result
);

/**
 * Returns the filesystem root node.
 *
 * The returned pointer is borrowed. The filesystem retains the owning
 * reference for as long as it remains mounted.
 *
 * @param filesystem Mounted initramfs filesystem.
 *
 * @return Borrowed root directory node, or NULL for invalid input.
 */
struct vfs_node *initramfs_root(
    const struct initramfs *filesystem
);

/**
 * Creates one kernel-owned directory inside a mounted initramfs namespace.
 *
 * parent must be a directory owned by filesystem. name is one ordinary VFS
 * pathname component and is copied into kernel-owned storage.
 *
 * The created directory is not backed by the immutable CPIO archive. It is
 * nevertheless owned by the initramfs and follows the same VFS node lifetime
 * rules as archive-backed directories.
 *
 * On success, result receives a borrowed pointer. The filesystem retains the
 * owning reference.
 *
 * @param filesystem Mounted initramfs filesystem.
 * @param parent Initramfs-owned directory receiving the new child.
 * @param name Ordinary pathname component.
 * @param name_length Number of component bytes.
 * @param result Receives the borrowed created directory.
 *
 * @return Detailed namespace mutation result.
 */
enum initramfs_namespace_result initramfs_create_directory(
    struct initramfs *filesystem,
    struct vfs_node *parent,
    const char *name,
    size_t name_length,
    struct vfs_node **result
);

/**
 * Attaches one externally owned non-directory VFS node beneath an initramfs
 * directory.
 *
 * The initramfs acquires one independent VFS node reference on success.
 * Device identity, implementation, and any other ownership behind child stay
 * completely outside the filesystem.
 *
 * Directory children are rejected. Attaching another filesystem directory is
 * mountpoint semantics and belongs to a future VFS mount layer.
 *
 * @param filesystem Mounted initramfs filesystem.
 * @param parent Initramfs-owned directory receiving the child.
 * @param name Ordinary pathname component copied by the filesystem.
 * @param name_length Number of component bytes.
 * @param child Live external non-directory VFS node.
 *
 * @return Detailed namespace mutation result.
 */
enum initramfs_namespace_result initramfs_attach_leaf(
    struct initramfs *filesystem,
    struct vfs_node *parent,
    const char *name,
    size_t name_length,
    struct vfs_node *child
);

/**
 * Releases one mounted initramfs filesystem.
 *
 * Unmount succeeds only when every filesystem node is held solely by the
 * filesystem itself. Outstanding VFS references therefore prevent teardown.
 *
 * A rejected unmount leaves the filesystem unchanged.
 *
 * @param filesystem Mounted initramfs filesystem.
 *
 * @return true when the filesystem was completely released; false when input
 *         is invalid or external node references remain.
 */
bool initramfs_unmount(
    struct initramfs *filesystem
);

#endif

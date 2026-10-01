// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file file.h
 * @brief Per-process descriptor-level file operations.
 *
 * These operations bridge process namespace and descriptor state with the
 * generic VFS. They operate exclusively on kernel addresses and do not form
 * part of the published userspace syscall ABI.
 */

#ifndef MYOS_PROCESS_FILE_H
#define MYOS_PROCESS_FILE_H

#include "instance.h"

#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

enum process_file_result {
    PROCESS_FILE_RESULT_SUCCESS,
    PROCESS_FILE_RESULT_INVALID_ARGUMENT,
    PROCESS_FILE_RESULT_BAD_DESCRIPTOR,
    PROCESS_FILE_RESULT_NOT_FOUND,
    PROCESS_FILE_RESULT_NO_NAMESPACE_ROOT,
    PROCESS_FILE_RESULT_NO_CURRENT_DIRECTORY,
    PROCESS_FILE_RESULT_NOT_DIRECTORY,
    PROCESS_FILE_RESULT_NOT_SUPPORTED,
    PROCESS_FILE_RESULT_ACCESS_DENIED,
    PROCESS_FILE_RESULT_RESOURCE_EXHAUSTED,
    PROCESS_FILE_RESULT_OVERFLOW,
};

/**
 * Opens one existing filesystem object in a process namespace.
 *
 * The pathname is resolved using the namespace root and current directory
 * owned by instance. On successful VFS open, the resulting open-file
 * description is installed in the lowest available descriptor slot.
 *
 * The descriptor table owns the resulting vfs_file reference. No additional
 * reference is returned to the caller.
 *
 * On failure, descriptor remains unchanged and no file or node reference
 * acquired by this operation remains owned.
 *
 * @param instance Process whose namespace and descriptor table are used.
 * @param path Pathname bytes.
 * @param path_length Number of pathname bytes.
 * @param access Requested VFS open access.
 * @param descriptor Receives the allocated descriptor.
 *
 * @return Process-level file operation result.
 */
enum process_file_result process_file_open(
    struct process_instance *instance,
    const char *path,
    size_t path_length,
    enum vfs_open_access access,
    size_t *descriptor
);

/**
 * Closes one descriptor owned by a process.
 *
 * @param instance Process whose descriptor table is used.
 * @param descriptor Descriptor to close.
 *
 * @return PROCESS_FILE_RESULT_SUCCESS when an occupied descriptor was closed,
 *         PROCESS_FILE_RESULT_BAD_DESCRIPTOR for an invalid or empty slot, or
 *         PROCESS_FILE_RESULT_INVALID_ARGUMENT for an invalid process.
 */
enum process_file_result process_file_close(
    struct process_instance *instance,
    size_t descriptor
);

/**
 * Reads bytes through one process file descriptor.
 *
 * buffer is a kernel address. Userspace copy-out belongs to the syscall layer.
 *
 * On success, bytes_read receives the number of bytes transferred. On failure,
 * bytes_read remains unchanged.
 */
enum process_file_result process_file_read(
    struct process_instance *instance,
    size_t descriptor,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

/**
 * Writes bytes through one process file descriptor.
 *
 * buffer is a kernel address. Userspace copy-in belongs to the syscall layer.
 *
 * On success, bytes_written receives the number of bytes transferred. On
 * failure, bytes_written remains unchanged.
 */
enum process_file_result process_file_write(
    struct process_instance *instance,
    size_t descriptor,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

/**
 * Changes the shared offset of one open-file description.
 *
 * On success, result receives the resulting absolute offset. On failure,
 * result remains unchanged.
 */
enum process_file_result process_file_seek(
    struct process_instance *instance,
    size_t descriptor,
    int64_t offset,
    enum vfs_seek_origin origin,
    uint64_t *result
);

/**
 * Changes the shared offset while enforcing a caller-supplied upper bound.
 *
 * If the VFS-computed offset exceeds maximum_offset, the original open-file
 * offset is restored and result remains unchanged.
 *
 * The bound is a kernel-internal policy supplied by the caller; it is not a
 * VFS limitation.
 */
enum process_file_result process_file_seek_bounded(
    struct process_instance *instance,
    size_t descriptor,
    int64_t offset,
    enum vfs_seek_origin origin,
    uint64_t maximum_offset,
    uint64_t *result
);

/**
 * Obtains metadata for the node referenced by one open descriptor.
 *
 * This is a descriptor-level stat operation. On success, result receives the
 * node metadata. On failure, result remains unchanged.
 */
enum process_file_result process_file_stat(
    struct process_instance *instance,
    size_t descriptor,
    struct vfs_stat *result
);

#endif

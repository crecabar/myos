// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file file.h
 * @brief Native bootstrap file-descriptor syscall wrappers.
 */

#ifndef MYOS_USER_RUNTIME_MYOS_FILE_H
#define MYOS_USER_RUNTIME_MYOS_FILE_H

#include <myos/abi/syscall.h>

#include <stddef.h>
#include <stdint.h>

/**
 * Opens one pathname through the current process namespace.
 *
 * path is an explicit byte sequence and need not be NUL-terminated.
 *
 * @param path Pathname bytes.
 * @param path_length Number of pathname bytes.
 * @param access Published SYSCALL_OPEN_ACCESS_* flags.
 *
 * @return New descriptor on success, or a negative error.
 */
syscall_result_t myos_fd_open(
    const char *path,
    size_t path_length,
    uint64_t access
);

/**
 * Closes one process file descriptor.
 *
 * @param descriptor Descriptor to close.
 *
 * @return Zero on success, or a negative error.
 */
syscall_result_t myos_fd_close(
    uint64_t descriptor
);

/**
 * Reads bytes through one process file descriptor.
 *
 * @param descriptor Descriptor to read.
 * @param buffer Destination buffer.
 * @param size Maximum number of bytes to read.
 *
 * @return Number of bytes read, or a negative error.
 */
syscall_result_t myos_fd_read(
    uint64_t descriptor,
    void *buffer,
    size_t size
);

/**
 * Writes bytes through one process file descriptor.
 *
 * @param descriptor Descriptor to write.
 * @param buffer Source buffer.
 * @param size Number of bytes to write.
 *
 * @return Number of bytes written, or a negative error.
 */
syscall_result_t myos_fd_write(
    uint64_t descriptor,
    const void *buffer,
    size_t size
);

/**
 * Changes one descriptor's shared open-file offset.
 *
 * @param descriptor Descriptor to seek.
 * @param offset Signed seek offset.
 * @param origin Published seek origin.
 *
 * @return Resulting absolute offset, or a negative error.
 */
syscall_result_t myos_fd_lseek(
    uint64_t descriptor,
    int64_t offset,
    enum syscall_seek_origin origin
);

/**
 * Obtains metadata for the node referenced by one descriptor.
 *
 * @param descriptor Descriptor to inspect.
 * @param status Metadata destination.
 *
 * @return Zero on success, or a negative error.
 */
syscall_result_t myos_fd_fstat(
    uint64_t descriptor,
    struct syscall_file_stat *status
);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file executable.h
 * @brief VFS-backed executable image acquisition.
 */

#ifndef MYOS_PROCESS_EXECUTABLE_H
#define MYOS_PROCESS_EXECUTABLE_H

#include "../elf/elf64.h"
#include "../vfs/vfs.h"

#include "instance.h"

#include <stdbool.h>
#include <stddef.h>

enum process_executable_result {
    PROCESS_EXECUTABLE_RESULT_SUCCESS,
    PROCESS_EXECUTABLE_RESULT_INVALID_ARGUMENT,
    PROCESS_EXECUTABLE_RESULT_NOT_FOUND,
    PROCESS_EXECUTABLE_RESULT_NO_NAMESPACE_ROOT,
    PROCESS_EXECUTABLE_RESULT_NO_CURRENT_DIRECTORY,
    PROCESS_EXECUTABLE_RESULT_NOT_DIRECTORY,
    PROCESS_EXECUTABLE_RESULT_NOT_REGULAR_FILE,
    PROCESS_EXECUTABLE_RESULT_NOT_SUPPORTED,
    PROCESS_EXECUTABLE_RESULT_ACCESS_DENIED,
    PROCESS_EXECUTABLE_RESULT_RESOURCE_EXHAUSTED,
    PROCESS_EXECUTABLE_RESULT_OVERFLOW,
    PROCESS_EXECUTABLE_RESULT_INVALID_IMAGE,
};

/**
 * Represents one ELF64 executable backed by an open VFS file.
 *
 * Before first use, the structure must be zero-initialized.
 *
 * A successfully opened executable owns exactly one vfs_file reference. The
 * file is kernel-private and is not installed in the process descriptor table.
 *
 * The embedded elf64_image borrows the backing-file source through this
 * object's file member. Therefore an open process_executable must remain at a
 * stable address until process_executable_close() is called.
 *
 * The backing file must remain open while the elf64_image is consumed because
 * ELF PT_LOAD bytes are read lazily while a process image is materialized.
 */
struct process_executable {
    struct vfs_file *file;
    struct elf64_image image;
};

/**
 * Resolves, opens, and parses one ELF64 executable from a process namespace.
 *
 * The pathname is resolved using the namespace root, current directory, and
 * mount topology owned or borrowed by instance.
 *
 * The resolved object must be a regular file. The resulting open-file
 * description is retained privately by executable and is not visible through
 * the process file descriptor table.
 *
 * executable must be zero-initialized and must not already own a backing file.
 *
 * On success, executable owns the backing file and its image is ready for
 * process-image construction. On failure, no VFS ownership acquired by this
 * operation remains held.
 *
 * @param instance Process namespace in which path is resolved.
 * @param path Executable pathname bytes.
 * @param path_length Number of pathname bytes.
 * @param executable Zero-initialized executable storage.
 *
 * @return Detailed executable acquisition result.
 */
enum process_executable_result process_executable_open_elf64(
    const struct process_instance *instance,
    const char *path,
    size_t path_length,
    struct process_executable *executable
);

/**
 * Releases one VFS-backed executable.
 *
 * After successful close, the backing file reference is released and the
 * embedded ELF descriptor is cleared. The storage may then be reused for a
 * later executable.
 *
 * @param executable Open executable to release.
 *
 * @return true when the executable was released; false for invalid state.
 */
bool process_executable_close(
    struct process_executable *executable
);

#endif

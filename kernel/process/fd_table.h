// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file fd_table.h
 * @brief Per-process file descriptor ownership table.
 *
 * Each occupied descriptor slot owns exactly one reference to a vfs_file.
 *
 * Descriptor lookup returns a borrowed file reference. Installing a file
 * retains it, while closing a descriptor or releasing the complete table
 * releases the reference owned by that slot.
 *
 * Descriptor allocation always chooses the lowest available descriptor.
 */

#ifndef MYOS_PROCESS_FD_TABLE_H
#define MYOS_PROCESS_FD_TABLE_H

#include "../vfs/vfs.h"

#include <stdbool.h>
#include <stddef.h>

#define PROCESS_FD_TABLE_CAPACITY 32U

/**
 * Owns the open-file references associated with one process.
 */
struct process_fd_table {
    struct vfs_file *entries[
        PROCESS_FD_TABLE_CAPACITY
    ];
};

/**
 * Initializes an empty descriptor table.
 *
 * @param table Table storage to initialize.
 *
 * @return true on success; false when table is NULL.
 */
bool process_fd_table_initialize(
    struct process_fd_table *table
);

/**
 * Installs one open-file reference in the lowest available descriptor.
 *
 * The table acquires one reference to file. The caller retains ownership of
 * any reference it already holds.
 *
 * On failure, the table and descriptor output remain unchanged.
 *
 * @param table Descriptor table.
 * @param file Live open-file description to install.
 * @param descriptor Receives the allocated descriptor.
 *
 * @return true on success; false for invalid input, a dead file, reference
 *         overflow, or a full table.
 */
bool process_fd_table_install(
    struct process_fd_table *table,
    struct vfs_file *file,
    size_t *descriptor
);

/**
 * Returns the file referenced by one descriptor.
 *
 * The returned pointer is borrowed. No reference count is changed.
 *
 * @param table Descriptor table.
 * @param descriptor Descriptor to inspect.
 *
 * @return Borrowed open-file pointer, or NULL when invalid or unoccupied.
 */
struct vfs_file *process_fd_table_get(
    const struct process_fd_table *table,
    size_t descriptor
);

/**
 * Closes one descriptor.
 *
 * The slot is made unavailable before releasing its owned file reference.
 *
 * @param table Descriptor table.
 * @param descriptor Descriptor to close.
 *
 * @return true when an occupied descriptor was closed; false otherwise.
 */
bool process_fd_table_close(
    struct process_fd_table *table,
    size_t descriptor
);

/**
 * Clones all descriptor ownership from another table.
 *
 * Destination must be empty. Every occupied source slot retains the same
 * vfs_file into the corresponding destination slot, preserving descriptor
 * numbers and shared open-file state.
 *
 * If any retain operation fails, all references acquired by this operation
 * are released and destination is left empty.
 *
 * @param destination Empty destination table.
 * @param source Source table.
 *
 * @return true when the complete table was cloned; false otherwise.
 */
bool process_fd_table_clone(
    struct process_fd_table *destination,
    const struct process_fd_table *source
);

/**
 * Releases every descriptor-owned file reference.
 *
 * All occupied slots are cleared. The function continues processing the
 * complete table even if an inconsistent file reference fails to release.
 *
 * @param table Descriptor table.
 *
 * @return true when every occupied slot released successfully; false for
 *         invalid input or an inconsistent reference.
 */
bool process_fd_table_release_all(
    struct process_fd_table *table
);

#endif

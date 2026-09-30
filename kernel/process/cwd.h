// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file cwd.h
 * @brief Per-process current working directory ownership.
 *
 * A non-NULL current_directory stored by process_instance owns exactly one
 * reference to a live VFS directory node.
 */

#ifndef MYOS_PROCESS_CWD_H
#define MYOS_PROCESS_CWD_H

#include "instance.h"

#include <stdbool.h>

/**
 * Returns the current working directory of one process instance.
 *
 * The returned pointer is borrowed. No reference count is changed.
 *
 * @param instance Process instance.
 *
 * @return Borrowed current-directory node, or NULL when none is assigned.
 */
struct vfs_node *process_cwd_get(
    const struct process_instance *instance
);

/**
 * Replaces one process instance's current working directory.
 *
 * directory must be a live VFS directory. The new node is retained before the
 * previous owned reference is released.
 *
 * Assigning the directory already installed is a successful no-op.
 *
 * On failure, the instance remains unchanged.
 *
 * @param instance Process instance.
 * @param directory Live directory to install.
 *
 * @return true when the requested directory is installed; false otherwise.
 */
bool process_cwd_set(
    struct process_instance *instance,
    struct vfs_node *directory
);

/**
 * Inherits current-directory ownership from another process instance.
 *
 * destination must not already own a current directory. When source has no
 * current directory, destination remains NULL and the operation succeeds.
 *
 * Otherwise destination acquires one independent reference to the same VFS
 * directory node.
 *
 * On failure, destination remains unchanged.
 *
 * @param destination Process instance receiving inherited ownership.
 * @param source Process instance whose current directory is inherited.
 *
 * @return true when inheritance completed; false otherwise.
 */
bool process_cwd_inherit(
    struct process_instance *destination,
    const struct process_instance *source
);

/**
 * Releases the current-directory reference owned by one process instance.
 *
 * Releasing an instance with no current directory is a successful no-op.
 *
 * @param instance Process instance.
 *
 * @return true when no current-directory ownership remains; false when the
 *         stored state is inconsistent.
 */
bool process_cwd_release(
    struct process_instance *instance
);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file namespace.h
 * @brief Per-process VFS namespace-root ownership.
 *
 * A non-NULL namespace_root stored by process_instance owns exactly one
 * reference to a live VFS directory node.
 */

#ifndef MYOS_PROCESS_NAMESPACE_H
#define MYOS_PROCESS_NAMESPACE_H

#include "instance.h"

#include <stdbool.h>

/**
 * Returns one process instance's namespace root.
 *
 * The returned pointer is borrowed. No reference count is changed.
 *
 * @param instance Process instance.
 *
 * @return Borrowed namespace-root node, or NULL when none is assigned.
 */
struct vfs_node *process_namespace_root_get(
    const struct process_instance *instance
);

/**
 * Installs the initial namespace root of one process instance.
 *
 * root must be a live VFS directory. The instance must not already own a
 * namespace root.
 *
 * On success, the process acquires one independent node reference.
 * On failure, the instance remains unchanged.
 *
 * Namespace-root replacement is deliberately not supported by this primitive.
 * A future operation that changes an established namespace must also preserve
 * current-directory containment semantics.
 *
 * @param instance Process instance.
 * @param root Live directory to install as namespace root.
 *
 * @return true when root ownership was installed; false otherwise.
 */
bool process_namespace_root_set(
    struct process_instance *instance,
    struct vfs_node *root
);

/**
 * Inherits namespace-root ownership from another process instance.
 *
 * destination must not already own a namespace root. When source has no
 * namespace root, destination remains NULL and the operation succeeds.
 *
 * Otherwise destination acquires one independent reference to the same
 * directory node.
 *
 * @param destination Process instance receiving inherited ownership.
 * @param source Process instance whose namespace root is inherited.
 *
 * @return true when inheritance completed; false otherwise.
 */
bool process_namespace_root_inherit(
    struct process_instance *destination,
    const struct process_instance *source
);

/**
 * Releases the namespace-root reference owned by one process instance.
 *
 * Releasing an instance with no namespace root is a successful no-op.
 *
 * @param instance Process instance.
 *
 * @return true when no namespace-root ownership remains; false when stored
 *         state is inconsistent.
 */
bool process_namespace_root_release(
    struct process_instance *instance
);

#endif

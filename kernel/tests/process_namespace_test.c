// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process_namespace_test.c
 * @brief Process namespace-root ownership regression tests.
 */

#include "process_namespace_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/namespace.h"
#include "../vfs/mount.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

static void process_namespace_test_set_release(void);

static void process_namespace_test_inheritance(void);

static void process_namespace_test_errors(void);

void process_namespace_test_run(void)
{
    process_namespace_test_set_release();
    process_namespace_test_inheritance();
    process_namespace_test_errors();

    diagnostics_write(
        "[process] Namespace root ownership tests passed\n"
    );
}

static void process_namespace_test_set_release(void)
{
    struct process_instance instance = {
        .namespace_root = NULL,
    };

    struct vfs_node root;

    if (!vfs_node_initialize(
        &root,
        VFS_NODE_TYPE_DIRECTORY,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize namespace root fixture"
        );
    }

    struct vfs_mount_table mounts;

    if (!vfs_mount_table_initialize(
        &mounts
    )) {
        kernel_panic(
            "Unable to initialize namespace mount topology fixture"
        );
    }

    if (
        process_namespace_root_get(
            &instance
        ) != NULL ||
        !process_namespace_root_set(
            &instance,
            &root,
            &mounts
        ) ||
        process_namespace_root_get(
            &instance
        ) != &root ||
        process_namespace_mounts_get(
            &instance
        ) != &mounts ||
        root.reference_count != 2
    ) {
        kernel_panic(
            "Process namespace root assignment failed"
        );
    }

    /*
     * Root installation is intentionally single-assignment.
     */
    if (
        process_namespace_root_set(
            &instance,
            &root,
            &mounts
        ) ||
        root.reference_count != 2
    ) {
        kernel_panic(
            "Process namespace root accepted replacement"
        );
    }

    if (
        !process_namespace_root_release(
            &instance
        ) ||
        process_namespace_root_get(
            &instance
        ) != NULL ||
        process_namespace_mounts_get(
            &instance
        ) != NULL ||
        root.reference_count != 1
    ) {
        kernel_panic(
            "Process namespace root release failed"
        );
    }

    if (!process_namespace_root_release(
        &instance
    )) {
        kernel_panic(
            "Empty namespace root release was not idempotent"
        );
    }

    if (!vfs_node_release(
        &root
    )) {
        kernel_panic(
            "Namespace root fixture cleanup failed"
        );
    }
}

static void process_namespace_test_inheritance(void)
{
    struct process_instance parent = {
        .namespace_root = NULL,
    };

    struct process_instance child = {
        .namespace_root = NULL,
    };

    struct vfs_node root;

    if (!vfs_node_initialize(
        &root,
        VFS_NODE_TYPE_DIRECTORY,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize namespace inheritance fixture"
        );
    }

    if (
        !process_namespace_root_inherit(
            &child,
            &parent
        ) ||
        child.namespace_root != NULL
    ) {
        kernel_panic(
            "Empty namespace inheritance failed"
        );
    }

    struct vfs_mount_table mounts;

    if (!vfs_mount_table_initialize(
        &mounts
    )) {
        kernel_panic(
            "Unable to initialize namespace mount topology fixture"
        );
    }

    if (
        !process_namespace_root_set(
            &parent,
            &root,
            &mounts
        ) ||
        process_namespace_mounts_get(
            &parent
        ) != &mounts ||
        root.reference_count != 2
    ) {
        kernel_panic(
            "Unable to assign parent namespace root"
        );
    }

    if (
        !process_namespace_root_inherit(
            &child,
            &parent
        ) ||
        process_namespace_mounts_get(
            &child
        ) != &mounts ||
        child.namespace_root != &root ||
        parent.namespace_root != &root ||
        root.reference_count != 3
    ) {
        kernel_panic(
            "Namespace root inheritance ownership failed"
        );
    }

    if (
        !process_namespace_root_release(
            &child
        ) ||
        root.reference_count != 2 ||
        parent.namespace_root != &root
    ) {
        kernel_panic(
            "Child namespace release affected parent"
        );
    }

    if (
        !process_namespace_root_release(
            &parent
        ) ||
        root.reference_count != 1 ||
        !vfs_node_release(
            &root
        )
    ) {
        kernel_panic(
            "Namespace inheritance fixture cleanup failed"
        );
    }
}

static void process_namespace_test_errors(void)
{
    struct process_instance instance = {
        .namespace_root = NULL,
    };

    struct process_instance destination = {
        .namespace_root = NULL,
    };

    struct vfs_node root;
    struct vfs_node regular;

    if (
        !vfs_node_initialize(
            &root,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &regular,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize namespace error fixture"
        );
    }

    struct vfs_mount_table mounts;

    if (!vfs_mount_table_initialize(
        &mounts
    )) {
        kernel_panic(
            "Unable to initialize namespace mount topology fixture"
        );
    }

    if (
        process_namespace_root_set(
            NULL,
            &root,
            &mounts
        ) ||
        process_namespace_root_set(
            &instance,
            NULL,
            &mounts
        ) ||
        process_namespace_root_set(
            &instance,
            &regular,
            &mounts
        ) ||
        instance.namespace_root != NULL ||
        root.reference_count != 1 ||
        regular.reference_count != 1
    ) {
        kernel_panic(
            "Namespace root accepted invalid assignment"
        );
    }

    root.reference_count =
        SIZE_MAX;

    if (
        process_namespace_root_set(
            &instance,
            &root,
            &mounts
        ) ||
        instance.namespace_root != NULL ||
        root.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "Namespace root accepted reference overflow"
        );
    }

    root.reference_count =
        1;

    if (!process_namespace_root_set(
        &instance,
        &root,
        &mounts
    )) {
        kernel_panic(
            "Unable to prepare namespace error ownership"
        );
    }

    if (
        process_namespace_root_inherit(
            &instance,
            &instance
        ) ||
        process_namespace_root_inherit(
            &instance,
            &destination
        ) ||
        instance.namespace_root != &root ||
        root.reference_count != 2
    ) {
        kernel_panic(
            "Namespace inheritance accepted invalid destination"
        );
    }

    root.reference_count =
        SIZE_MAX;

    if (
        process_namespace_root_inherit(
            &destination,
            &instance
        ) ||
        destination.namespace_root != NULL ||
        root.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "Namespace inheritance accepted reference overflow"
        );
    }

    root.reference_count =
        2;

    struct process_instance inconsistent = {
        .namespace_root =
            NULL,
        .namespace_mounts =
            &mounts,
    };

    if (
        process_namespace_root_inherit(
            &destination,
            &inconsistent
        ) ||
        destination.namespace_root != NULL ||
        destination.namespace_mounts != NULL
    ) {
        kernel_panic(
            "Namespace inheritance accepted partial source state"
        );
    }

    if (
        process_namespace_root_release(
            &inconsistent
        ) ||
        inconsistent.namespace_root != NULL ||
        inconsistent.namespace_mounts != &mounts
    ) {
        kernel_panic(
            "Namespace release accepted partial namespace state"
        );
    }

    /*
     * Borrowed mount topology requires no ownership cleanup.
     */
    inconsistent.namespace_mounts =
        NULL;

    if (
        !process_namespace_root_release(
            &instance
        ) ||
        root.reference_count != 1
    ) {
        kernel_panic(
            "Unable to release namespace error fixture"
        );
    }

    if (
        !vfs_node_release(
            &regular
        ) ||
        !vfs_node_release(
            &root
        )
    ) {
        kernel_panic(
            "Namespace error fixture cleanup failed"
        );
    }
}

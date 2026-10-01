// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process_cwd_test.c
 * @brief Process current-working-directory ownership regression tests.
 */

#include "process_cwd_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/cwd.h"
#include "../process/instance.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

static void process_cwd_test_set_replace_release(void);

static void process_cwd_test_inheritance(void);

static void process_cwd_test_errors(void);

void process_cwd_test_run(void)
{
    process_cwd_test_set_replace_release();
    process_cwd_test_inheritance();
    process_cwd_test_errors();

    diagnostics_write(
        "[process] Current working directory ownership tests passed\n"
    );
}

static void process_cwd_test_set_replace_release(void)
{
    struct process_instance instance = {
        .current_directory = NULL,
    };

    struct vfs_node first;
    struct vfs_node second;

    if (
        !vfs_node_initialize(
            &first,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &second,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize process CWD fixture"
        );
    }

    if (
        process_cwd_get(
            &instance
        ) != NULL ||
        !process_cwd_set(
            &instance,
            &first
        ) ||
        process_cwd_get(
            &instance
        ) != &first ||
        first.reference_count != 2
    ) {
        kernel_panic(
            "Process CWD initial assignment failed"
        );
    }

    /*
     * Reinstalling the same directory is a no-op.
     */
    if (
        !process_cwd_set(
            &instance,
            &first
        ) ||
        first.reference_count != 2
    ) {
        kernel_panic(
            "Process CWD same-directory assignment changed ownership"
        );
    }

    if (
        !process_cwd_set(
            &instance,
            &second
        ) ||
        process_cwd_get(
            &instance
        ) != &second ||
        first.reference_count != 1 ||
        second.reference_count != 2
    ) {
        kernel_panic(
            "Process CWD replacement ownership failed"
        );
    }

    if (
        !process_cwd_release(
            &instance
        ) ||
        process_cwd_get(
            &instance
        ) != NULL ||
        second.reference_count != 1
    ) {
        kernel_panic(
            "Process CWD release failed"
        );
    }

    if (!process_cwd_release(
        &instance
    )) {
        kernel_panic(
            "Empty process CWD release was not idempotent"
        );
    }

    if (
        !vfs_node_release(
            &second
        ) ||
        !vfs_node_release(
            &first
        )
    ) {
        kernel_panic(
            "Process CWD fixture cleanup failed"
        );
    }
}

static void process_cwd_test_inheritance(void)
{
    struct process_instance parent = {
        .current_directory = NULL,
    };

    struct process_instance child = {
        .current_directory = NULL,
    };

    struct vfs_node directory;

    if (!vfs_node_initialize(
        &directory,
        VFS_NODE_TYPE_DIRECTORY,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize process CWD inheritance fixture"
        );
    }

    /*
     * NULL CWD inheritance succeeds without manufacturing ownership.
     */
    if (
        !process_cwd_inherit(
            &child,
            &parent
        ) ||
        child.current_directory != NULL
    ) {
        kernel_panic(
            "Process CWD empty inheritance failed"
        );
    }

    if (
        !process_cwd_set(
            &parent,
            &directory
        ) ||
        directory.reference_count != 2
    ) {
        kernel_panic(
            "Unable to assign parent process CWD"
        );
    }

    if (
        !process_cwd_inherit(
            &child,
            &parent
        ) ||
        child.current_directory !=
            &directory ||
        parent.current_directory !=
            &directory ||
        directory.reference_count != 3
    ) {
        kernel_panic(
            "Process CWD inheritance ownership failed"
        );
    }

    if (
        !process_cwd_release(
            &child
        ) ||
        directory.reference_count != 2 ||
        parent.current_directory !=
            &directory
    ) {
        kernel_panic(
            "Child CWD release affected parent ownership"
        );
    }

    if (
        !process_cwd_release(
            &parent
        ) ||
        directory.reference_count != 1 ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "Process CWD inheritance fixture cleanup failed"
        );
    }
}

static void process_cwd_test_errors(void)
{
    struct process_instance instance = {
        .current_directory = NULL,
    };

    struct process_instance destination = {
        .current_directory = NULL,
    };

    struct vfs_node directory;
    struct vfs_node regular;

    if (
        !vfs_node_initialize(
            &directory,
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
            "Unable to initialize process CWD error fixture"
        );
    }

    if (
        process_cwd_set(
            NULL,
            &directory
        ) ||
        process_cwd_set(
            &instance,
            NULL
        ) ||
        process_cwd_set(
            &instance,
            &regular
        ) ||
        instance.current_directory != NULL ||
        directory.reference_count != 1 ||
        regular.reference_count != 1
    ) {
        kernel_panic(
            "Process CWD accepted invalid assignment"
        );
    }

    directory.reference_count =
        SIZE_MAX;

    if (
        process_cwd_set(
            &instance,
            &directory
        ) ||
        instance.current_directory != NULL ||
        directory.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "Process CWD accepted reference overflow"
        );
    }

    directory.reference_count =
        1;

    if (!process_cwd_set(
        &instance,
        &directory
    )) {
        kernel_panic(
            "Unable to prepare process CWD error ownership"
        );
    }

    if (
        process_cwd_inherit(
            &instance,
            &instance
        ) ||
        process_cwd_inherit(
            &instance,
            &destination
        ) ||
        instance.current_directory !=
            &directory ||
        directory.reference_count != 2
    ) {
        kernel_panic(
            "Process CWD inheritance accepted invalid destination"
        );
    }

    directory.reference_count =
        SIZE_MAX;

    if (
        process_cwd_inherit(
            &destination,
            &instance
        ) ||
        destination.current_directory != NULL ||
        directory.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "Process CWD inheritance accepted reference overflow"
        );
    }

    directory.reference_count =
        2;

    if (
        !process_cwd_release(
            &instance
        ) ||
        directory.reference_count != 1
    ) {
        kernel_panic(
            "Unable to release process CWD error fixture"
        );
    }

    if (
        !vfs_node_release(
            &regular
        ) ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "Process CWD error fixture cleanup failed"
        );
    }
}

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file vfs_parent_test.c
 * @brief VFS directory parent traversal regression tests.
 */

#include "vfs_parent_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

struct vfs_parent_test_directory {
    struct vfs_node *parent;
    size_t parent_count;
};

static struct vfs_node *vfs_parent_test_parent(
    struct vfs_node *directory
);

static void vfs_parent_test_success(void);

static void vfs_parent_test_errors(void);

static void vfs_parent_test_reference_failure(void);

void vfs_parent_test_run(void)
{
    vfs_parent_test_success();
    vfs_parent_test_errors();
    vfs_parent_test_reference_failure();

    diagnostics_write(
        "[vfs] Directory parent contract tests passed\n"
    );
}

static struct vfs_node *vfs_parent_test_parent(
    struct vfs_node *directory)
{
    if (directory == NULL) {
        kernel_panic(
            "VFS parent test callback received NULL"
        );
    }

    struct vfs_parent_test_directory *fixture =
        directory->private_data;

    if (fixture == NULL) {
        kernel_panic(
            "VFS parent test directory has no fixture"
        );
    }

    ++fixture->parent_count;

    return
        fixture->parent;
}

static void vfs_parent_test_success(void)
{
    struct vfs_node root;
    struct vfs_node child;

    struct vfs_parent_test_directory root_fixture = {
        .parent = NULL,
        .parent_count = 0,
    };

    struct vfs_parent_test_directory child_fixture = {
        .parent = &root,
        .parent_count = 0,
    };

    struct vfs_node_operations operations = {
        .lookup = NULL,
        .parent =
            vfs_parent_test_parent,
        .destroy = NULL,
    };

    if (
        !vfs_node_initialize(
            &root,
            VFS_NODE_TYPE_DIRECTORY,
            &operations,
            &root_fixture
        ) ||
        !vfs_node_initialize(
            &child,
            VFS_NODE_TYPE_DIRECTORY,
            &operations,
            &child_fixture
        )
    ) {
        kernel_panic(
            "Unable to initialize VFS parent fixture"
        );
    }

    struct vfs_node *result =
        NULL;

    if (
        vfs_node_parent(
            &child,
            &result
        ) != VFS_PARENT_RESULT_FOUND ||
        result != &root ||
        root.reference_count != 2 ||
        child_fixture.parent_count != 1
    ) {
        kernel_panic(
            "VFS parent traversal did not return retained parent"
        );
    }

    if (
        !vfs_node_release(
            result
        ) ||
        root.reference_count != 1
    ) {
        kernel_panic(
            "VFS parent result ownership cleanup failed"
        );
    }

    struct vfs_node *sentinel =
        &child;

    if (
        vfs_node_parent(
            &root,
            &sentinel
        ) != VFS_PARENT_RESULT_NO_PARENT ||
        sentinel != &child ||
        root_fixture.parent_count != 1
    ) {
        kernel_panic(
            "VFS root-style no-parent contract failed"
        );
    }

    if (
        !vfs_node_release(
            &child
        ) ||
        !vfs_node_release(
            &root
        )
    ) {
        kernel_panic(
            "VFS parent fixture cleanup failed"
        );
    }
}

static void vfs_parent_test_errors(void)
{
    struct vfs_node directory;
    struct vfs_node regular;
    struct vfs_node unsupported;
    struct vfs_node sentinel;

    struct vfs_parent_test_directory fixture = {
        .parent = NULL,
        .parent_count = 0,
    };

    struct vfs_node_operations operations = {
        .lookup = NULL,
        .parent =
            vfs_parent_test_parent,
        .destroy = NULL,
    };

    if (
        !vfs_node_initialize(
            &directory,
            VFS_NODE_TYPE_DIRECTORY,
            &operations,
            &fixture
        ) ||
        !vfs_node_initialize(
            &regular,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &unsupported,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &sentinel,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize VFS parent error fixture"
        );
    }

    struct vfs_node *result =
        &sentinel;

    if (
        vfs_node_parent(
            NULL,
            &result
        ) != VFS_PARENT_RESULT_INVALID_ARGUMENT ||
        vfs_node_parent(
            &directory,
            NULL
        ) != VFS_PARENT_RESULT_INVALID_ARGUMENT ||
        vfs_node_parent(
            &regular,
            &result
        ) != VFS_PARENT_RESULT_NOT_DIRECTORY ||
        vfs_node_parent(
            &unsupported,
            &result
        ) != VFS_PARENT_RESULT_NOT_SUPPORTED
    ) {
        kernel_panic(
            "VFS parent traversal accepted invalid operation"
        );
    }

    if (
        result != &sentinel ||
        fixture.parent_count != 0
    ) {
        kernel_panic(
            "Rejected VFS parent traversal mutated observable state"
        );
    }

    /*
     * A dead directory cannot participate in traversal. Restore the synthetic
     * lifetime state afterward so the fixture can be cleaned up normally.
     */
    directory.reference_count =
        0;

    if (
        vfs_node_parent(
            &directory,
            &result
        ) != VFS_PARENT_RESULT_INVALID_ARGUMENT ||
        result != &sentinel ||
        fixture.parent_count != 0
    ) {
        kernel_panic(
            "VFS parent traversal accepted dead directory"
        );
    }

    directory.reference_count =
        1;

    if (
        !vfs_node_release(
            &sentinel
        ) ||
        !vfs_node_release(
            &unsupported
        ) ||
        !vfs_node_release(
            &regular
        ) ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "VFS parent error fixture cleanup failed"
        );
    }
}

static void vfs_parent_test_reference_failure(void)
{
    struct vfs_node parent;
    struct vfs_node child;
    struct vfs_node sentinel;

    struct vfs_parent_test_directory child_fixture = {
        .parent = &parent,
        .parent_count = 0,
    };

    struct vfs_node_operations operations = {
        .lookup = NULL,
        .parent =
            vfs_parent_test_parent,
        .destroy = NULL,
    };

    if (
        !vfs_node_initialize(
            &parent,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &child,
            VFS_NODE_TYPE_DIRECTORY,
            &operations,
            &child_fixture
        ) ||
        !vfs_node_initialize(
            &sentinel,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize VFS parent reference fixture"
        );
    }

    parent.reference_count =
        SIZE_MAX;

    struct vfs_node *result =
        &sentinel;

    if (
        vfs_node_parent(
            &child,
            &result
        ) != VFS_PARENT_RESULT_RESOURCE_EXHAUSTED ||
        result != &sentinel ||
        parent.reference_count != SIZE_MAX ||
        child_fixture.parent_count != 1
    ) {
        kernel_panic(
            "VFS parent reference failure contract failed"
        );
    }

    parent.reference_count =
        1;

    if (
        !vfs_node_release(
            &sentinel
        ) ||
        !vfs_node_release(
            &child
        ) ||
        !vfs_node_release(
            &parent
        )
    ) {
        kernel_panic(
            "VFS parent reference fixture cleanup failed"
        );
    }
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file vfs_node_test.c
 * @brief VFS node ownership and lifetime regression tests.
 */

#include "vfs_node_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

// Static local variables
static size_t vfs_node_test_destroy_count;
static void *vfs_node_test_destroy_private_data;

// Private functions and helpers declarations
static void vfs_node_test_destroy(
    struct vfs_node *node
);

static void vfs_node_test_initialization(void);

static void vfs_node_test_reference_lifetime(void);

static void vfs_node_test_optional_destroy(void);

static void vfs_node_test_reference_overflow(void);

// Public functions implementations
void vfs_node_test_run(void)
{
    vfs_node_test_initialization();
    vfs_node_test_reference_lifetime();
    vfs_node_test_optional_destroy();
    vfs_node_test_reference_overflow();

    diagnostics_write(
        "[vfs] Node lifetime contract tests passed\n"
    );
}

// Private functions and helpers implementations
static void vfs_node_test_destroy(
    struct vfs_node *node)
{
    if (node == NULL) {
        kernel_panic(
            "VFS node destroy callback received NULL"
        );
    }

    ++vfs_node_test_destroy_count;

    vfs_node_test_destroy_private_data =
        node->private_data;
}

static void vfs_node_test_initialization(void)
{
    struct vfs_node node;

    uint64_t private_value =
        0x1122334455667788ULL;

    struct vfs_node_operations operations = {
        .destroy =
            vfs_node_test_destroy,
    };

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_REGULAR_FILE,
        &operations,
        &private_value
    )) {
        kernel_panic(
            "VFS node initialization rejected valid node"
        );
    }

    if (
        node.type !=
            VFS_NODE_TYPE_REGULAR_FILE ||
        node.reference_count != 1 ||
        node.operations !=
            &operations ||
        node.private_data !=
            &private_value
    ) {
        kernel_panic(
            "VFS node initialization produced invalid state"
        );
    }

    if (vfs_node_initialize(
        NULL,
        VFS_NODE_TYPE_REGULAR_FILE,
        &operations,
        &private_value
    )) {
        kernel_panic(
            "VFS node initialization accepted NULL"
        );
    }

    if (vfs_node_initialize(
        &node,
        (enum vfs_node_type) 99,
        &operations,
        &private_value
    )) {
        kernel_panic(
            "VFS node initialization accepted invalid type"
        );
    }
}

static void vfs_node_test_reference_lifetime(void)
{
    struct vfs_node node;

    uint64_t private_value =
        0xaabbccddeeff0011ULL;

    struct vfs_node_operations operations = {
        .destroy =
            vfs_node_test_destroy,
    };

    vfs_node_test_destroy_count =
        0;

    vfs_node_test_destroy_private_data =
        NULL;

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_DIRECTORY,
        &operations,
        &private_value
    )) {
        kernel_panic(
            "VFS node lifetime initialization failed"
        );
    }

    if (
        !vfs_node_retain(&node) ||
        !vfs_node_retain(&node) ||
        node.reference_count != 3
    ) {
        kernel_panic(
            "VFS node retain contract failed"
        );
    }

    if (
        !vfs_node_release(&node) ||
        node.reference_count != 2 ||
        vfs_node_test_destroy_count != 0
    ) {
        kernel_panic(
            "VFS node first release contract failed"
        );
    }

    if (
        !vfs_node_release(&node) ||
        node.reference_count != 1 ||
        vfs_node_test_destroy_count != 0
    ) {
        kernel_panic(
            "VFS node second release contract failed"
        );
    }

    if (!vfs_node_release(
        &node
    )) {
        kernel_panic(
            "VFS node final release failed"
        );
    }

    if (
        node.reference_count != 0 ||
        vfs_node_test_destroy_count != 1 ||
        vfs_node_test_destroy_private_data !=
            &private_value
    ) {
        kernel_panic(
            "VFS node final release lifetime mismatch"
        );
    }

    if (vfs_node_retain(
        &node
    )) {
        kernel_panic(
            "VFS node retained after lifetime ended"
        );
    }

    if (vfs_node_release(
        &node
    )) {
        kernel_panic(
            "VFS node released after lifetime ended"
        );
    }

    if (
        vfs_node_test_destroy_count != 1
    ) {
        kernel_panic(
            "VFS node destroy callback ran more than once"
        );
    }
}

static void vfs_node_test_optional_destroy(void)
{
    struct vfs_node node;

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_CHARACTER_DEVICE,
        NULL,
        NULL
    )) {
        kernel_panic(
            "VFS node rejected optional operations"
        );
    }

    if (
        !vfs_node_release(&node) ||
        node.reference_count != 0
    ) {
        kernel_panic(
            "VFS node without destroy failed final release"
        );
    }
}

static void vfs_node_test_reference_overflow(void)
{
    struct vfs_node node;

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_REGULAR_FILE,
        NULL,
        NULL
    )) {
        kernel_panic(
            "VFS node overflow fixture initialization failed"
        );
    }

    node.reference_count =
        SIZE_MAX;

    if (vfs_node_retain(
        &node
    )) {
        kernel_panic(
            "VFS node reference count overflow accepted"
        );
    }

    if (
        node.reference_count !=
        SIZE_MAX
    ) {
        kernel_panic(
            "VFS node overflow rejection mutated count"
        );
    }
}

// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file vfs_file_test.c
 * @brief VFS open-file ownership and lifetime regression tests.
 */

#include "vfs_file_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

// Static local variables
static size_t vfs_file_test_node_destroy_count;
static size_t vfs_file_test_file_destroy_count;
static size_t vfs_file_test_destroy_node_reference_count;
static void *vfs_file_test_destroy_private_data;

// Private functions and helpers declarations
static void vfs_file_test_node_destroy(
    struct vfs_node *node
);

static void vfs_file_test_file_destroy(
    struct vfs_file *file
);

static void vfs_file_test_initialization(void);

static void vfs_file_test_reference_lifetime(void);

static void vfs_file_test_shared_node_state(void);

static void vfs_file_test_initialization_rollback(void);

static void vfs_file_test_optional_destroy(void);

static void vfs_file_test_reference_overflow(void);

// Public functions implementations
void vfs_file_test_run(void)
{
    vfs_file_test_initialization();
    vfs_file_test_reference_lifetime();
    vfs_file_test_shared_node_state();
    vfs_file_test_initialization_rollback();
    vfs_file_test_optional_destroy();
    vfs_file_test_reference_overflow();

    diagnostics_write(
        "[vfs] Open-file lifetime contract tests passed\n"
    );
}

// Private functions and helpers implementations
static void vfs_file_test_node_destroy(
    struct vfs_node *node)
{
    if (node == NULL) {
        kernel_panic(
            "VFS file test node destroy callback received NULL"
        );
    }

    ++vfs_file_test_node_destroy_count;
}

static void vfs_file_test_file_destroy(
    struct vfs_file *file)
{
    if (
        file == NULL ||
        file->node == NULL
    ) {
        kernel_panic(
            "VFS file destroy callback received invalid file"
        );
    }

    ++vfs_file_test_file_destroy_count;

    vfs_file_test_destroy_node_reference_count =
        file->node->reference_count;

    vfs_file_test_destroy_private_data =
        file->private_data;
}

static void vfs_file_test_initialization(void)
{
    struct vfs_node node;
    struct vfs_file file;

    uint64_t private_value =
        0x1122334455667788ULL;

    struct vfs_file_operations operations = {
        .destroy =
            vfs_file_test_file_destroy,
    };

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_REGULAR_FILE,
        NULL,
        NULL
    )) {
        kernel_panic(
            "VFS file initialization fixture node failed"
        );
    }

    if (!vfs_file_initialize(
        &file,
        &node,
        VFS_OPEN_ACCESS_READ |
            VFS_OPEN_ACCESS_WRITE,
        &operations,
        &private_value
    )) {
        kernel_panic(
            "VFS file initialization rejected valid file"
        );
    }

    if (
        file.reference_count != 1 ||
        file.node != &node ||
        file.offset != 0 ||
        file.access !=
            (
                VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE
            ) ||
        file.operations != &operations ||
        file.private_data != &private_value ||
        node.reference_count != 2
    ) {
        kernel_panic(
            "VFS file initialization produced invalid state"
        );
    }

    if (!vfs_file_release(
        &file
    )) {
        kernel_panic(
            "VFS file initialization fixture release failed"
        );
    }

    if (
        node.reference_count != 1 ||
        !vfs_node_release(&node)
    ) {
        kernel_panic(
            "VFS file initialization leaked node reference"
        );
    }
}

static void vfs_file_test_reference_lifetime(void)
{
    struct vfs_node node;
    struct vfs_file file;

    uint64_t private_value =
        0xaabbccddeeff0011ULL;

    struct vfs_node_operations node_operations = {
        .destroy =
            vfs_file_test_node_destroy,
    };

    struct vfs_file_operations file_operations = {
        .destroy =
            vfs_file_test_file_destroy,
    };

    vfs_file_test_node_destroy_count =
        0;

    vfs_file_test_file_destroy_count =
        0;

    vfs_file_test_destroy_node_reference_count =
        0;

    vfs_file_test_destroy_private_data =
        NULL;

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_REGULAR_FILE,
        &node_operations,
        NULL
    )) {
        kernel_panic(
            "VFS file lifetime fixture node failed"
        );
    }

    if (!vfs_file_initialize(
        &file,
        &node,
        VFS_OPEN_ACCESS_READ,
        &file_operations,
        &private_value
    )) {
        kernel_panic(
            "VFS file lifetime initialization failed"
        );
    }

    if (
        node.reference_count != 2 ||
        !vfs_file_retain(&file) ||
        !vfs_file_retain(&file) ||
        file.reference_count != 3
    ) {
        kernel_panic(
            "VFS file retain contract failed"
        );
    }

    if (
        !vfs_file_release(&file) ||
        file.reference_count != 2 ||
        node.reference_count != 2 ||
        vfs_file_test_file_destroy_count != 0
    ) {
        kernel_panic(
            "VFS file first release contract failed"
        );
    }

    if (
        !vfs_file_release(&file) ||
        file.reference_count != 1 ||
        node.reference_count != 2 ||
        vfs_file_test_file_destroy_count != 0
    ) {
        kernel_panic(
            "VFS file second release contract failed"
        );
    }

    if (!vfs_file_release(
        &file
    )) {
        kernel_panic(
            "VFS file final release failed"
        );
    }

    if (
        file.reference_count != 0 ||
        node.reference_count != 1 ||
        vfs_file_test_file_destroy_count != 1 ||
        vfs_file_test_node_destroy_count != 0 ||
        vfs_file_test_destroy_node_reference_count != 2 ||
        vfs_file_test_destroy_private_data !=
            &private_value
    ) {
        kernel_panic(
            "VFS file final release lifetime mismatch"
        );
    }

    if (vfs_file_retain(
        &file
    )) {
        kernel_panic(
            "VFS file retained after lifetime ended"
        );
    }

    if (vfs_file_release(
        &file
    )) {
        kernel_panic(
            "VFS file released after lifetime ended"
        );
    }

    if (
        vfs_file_test_file_destroy_count != 1
    ) {
        kernel_panic(
            "VFS file destroy callback ran more than once"
        );
    }

    if (
        !vfs_node_release(&node) ||
        vfs_file_test_node_destroy_count != 1
    ) {
        kernel_panic(
            "VFS file final node ownership release failed"
        );
    }
}

static void vfs_file_test_shared_node_state(void)
{
    struct vfs_node node;
    struct vfs_file first_file;
    struct vfs_file second_file;

    struct vfs_node_operations node_operations = {
        .destroy =
            vfs_file_test_node_destroy,
    };

    vfs_file_test_node_destroy_count =
        0;

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_REGULAR_FILE,
        &node_operations,
        NULL
    )) {
        kernel_panic(
            "VFS shared-node fixture initialization failed"
        );
    }

    if (
        !vfs_file_initialize(
            &first_file,
            &node,
            VFS_OPEN_ACCESS_READ,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &second_file,
            &node,
            VFS_OPEN_ACCESS_WRITE,
            NULL,
            NULL
        ) ||
        node.reference_count != 3
    ) {
        kernel_panic(
            "VFS shared-node file initialization failed"
        );
    }

    first_file.offset =
        11;

    second_file.offset =
        97;

    if (
        first_file.node != second_file.node ||
        first_file.offset != 11 ||
        second_file.offset != 97 ||
        first_file.access !=
            VFS_OPEN_ACCESS_READ ||
        second_file.access !=
            VFS_OPEN_ACCESS_WRITE
    ) {
        kernel_panic(
            "VFS per-open state isolation failed"
        );
    }

    if (
        !vfs_node_release(&node) ||
        node.reference_count != 2
    ) {
        kernel_panic(
            "VFS shared-node external owner release failed"
        );
    }

    if (
        !vfs_file_release(&first_file) ||
        node.reference_count != 1 ||
        vfs_file_test_node_destroy_count != 0
    ) {
        kernel_panic(
            "VFS shared-node first file release failed"
        );
    }

    if (
        !vfs_file_release(&second_file) ||
        vfs_file_test_node_destroy_count != 1
    ) {
        kernel_panic(
            "VFS shared-node final file release failed"
        );
    }
}

static void vfs_file_test_initialization_rollback(void)
{
    struct vfs_node node;

    struct vfs_file file = {
        .reference_count = 7,
        .node = NULL,
        .offset = 41,
        .access = VFS_OPEN_ACCESS_WRITE,
        .operations = NULL,
        .private_data = &node,
    };

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_REGULAR_FILE,
        NULL,
        NULL
    )) {
        kernel_panic(
            "VFS file rollback fixture node failed"
        );
    }

    node.reference_count =
        SIZE_MAX;

    if (vfs_file_initialize(
        &file,
        &node,
        VFS_OPEN_ACCESS_READ,
        NULL,
        NULL
    )) {
        kernel_panic(
            "VFS file initialization accepted node overflow"
        );
    }

    if (
        file.reference_count != 7 ||
        file.node != NULL ||
        file.offset != 41 ||
        file.access != VFS_OPEN_ACCESS_WRITE ||
        file.operations != NULL ||
        file.private_data != &node ||
        node.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "VFS file initialization rollback mutated state"
        );
    }

    /*
     * Restore the deliberately corrupted reference count before testing
     * independent argument and access validation failures.
     */
    node.reference_count =
        1;

    if (vfs_file_initialize(
        NULL,
        &node,
        VFS_OPEN_ACCESS_READ,
        NULL,
        NULL
    )) {
        kernel_panic(
            "VFS file initialization accepted NULL file"
        );
    }

    if (vfs_file_initialize(
        &file,
        NULL,
        VFS_OPEN_ACCESS_READ,
        NULL,
        NULL
    )) {
        kernel_panic(
            "VFS file initialization accepted NULL node"
        );
    }

    if (vfs_file_initialize(
        &file,
        &node,
        0,
        NULL,
        NULL
    )) {
        kernel_panic(
            "VFS file initialization accepted empty access mode"
        );
    }

    if (file.reference_count != 7) {
        kernel_panic(
            "Invalid VFS access mode mutated file reference count"
        );
    }

    if (file.node != NULL) {
        kernel_panic(
            "Invalid VFS access mode mutated file node"
        );
    }

    if (file.offset != 41) {
        kernel_panic(
            "Invalid VFS access mode mutated file offset"
        );
    }

    if (
        file.access !=
        VFS_OPEN_ACCESS_WRITE
    ) {
        kernel_panic(
            "Invalid VFS access mode mutated file access"
        );
    }

    if (file.operations != NULL) {
        kernel_panic(
            "Invalid VFS access mode mutated file operations"
        );
    }

    if (file.private_data != &node) {
        kernel_panic(
            "Invalid VFS access mode mutated file private data"
        );
    }

    if (node.reference_count != 1) {
        kernel_panic(
            "Invalid VFS access mode mutated node reference count"
        );
    }

    enum vfs_open_access invalid_access =
        (enum vfs_open_access) (
            VFS_OPEN_ACCESS_READ |
            (1U << 2)
        );

    if (vfs_file_initialize(
        &file,
        &node,
        invalid_access,
        NULL,
        NULL
    )) {
        kernel_panic(
            "VFS file initialization accepted unknown access bits"
        );
    }

    if (
        file.reference_count != 7 ||
        file.node != NULL ||
        file.offset != 41 ||
        file.access !=
            VFS_OPEN_ACCESS_WRITE ||
        file.operations != NULL ||
        file.private_data != &node ||
        node.reference_count != 1
    ) {
        kernel_panic(
            "Unknown VFS access bits mutated initialization state"
        );
    }

    /*
     * The following release deliberately ends the node lifetime so the next
     * regression can verify that file initialization cannot resurrect it.
     */
    if (!vfs_node_release(
        &node
    )) {
        kernel_panic(
            "VFS file rollback fixture cleanup failed"
        );
    }

    if (vfs_file_initialize(
        &file,
        &node,
        VFS_OPEN_ACCESS_READ,
        NULL,
        NULL
    )) {
        kernel_panic(
            "VFS file initialization retained dead node"
        );
    }
}

static void vfs_file_test_optional_destroy(void)
{
    struct vfs_node node;
    struct vfs_file file;

    if (
        !vfs_node_initialize(
            &node,
            VFS_NODE_TYPE_CHARACTER_DEVICE,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &file,
            &node,
            VFS_OPEN_ACCESS_WRITE,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "VFS file optional destroy fixture failed"
        );
    }

    if (
        !vfs_file_release(&file) ||
        file.reference_count != 0 ||
        node.reference_count != 1
    ) {
        kernel_panic(
            "VFS file without destroy failed final release"
        );
    }

    if (!vfs_node_release(
        &node
    )) {
        kernel_panic(
            "VFS file optional destroy node cleanup failed"
        );
    }
}

static void vfs_file_test_reference_overflow(void)
{
    struct vfs_node node;
    struct vfs_file file;

    if (
        !vfs_node_initialize(
            &node,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &file,
            &node,
            VFS_OPEN_ACCESS_READ,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "VFS file overflow fixture initialization failed"
        );
    }

    file.reference_count =
        SIZE_MAX;

    if (vfs_file_retain(
        &file
    )) {
        kernel_panic(
            "VFS file reference count overflow accepted"
        );
    }

    if (
        file.reference_count !=
        SIZE_MAX
    ) {
        kernel_panic(
            "VFS file overflow rejection mutated count"
        );
    }

    file.reference_count =
        1;

    if (
        !vfs_file_release(&file) ||
        node.reference_count != 1 ||
        !vfs_node_release(&node)
    ) {
        kernel_panic(
            "VFS file overflow fixture cleanup failed"
        );
    }
}

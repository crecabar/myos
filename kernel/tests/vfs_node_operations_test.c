// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file vfs_node_operations_test.c
 * @brief VFS node open and stat operation regression tests.
 */

#include "vfs_node_operations_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

// Static local variables
static size_t vfs_node_operations_test_open_call_count;
static size_t vfs_node_operations_test_stat_call_count;

static struct vfs_file *
    vfs_node_operations_test_open_storage;

// Private functions and helpers declarations
static enum vfs_open_result
vfs_node_operations_test_open_success(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result
);

static enum vfs_open_result
vfs_node_operations_test_open_denied(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result
);

static enum vfs_open_result
vfs_node_operations_test_open_empty_success(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result
);

static enum vfs_stat_result
vfs_node_operations_test_stat_success(
    struct vfs_node *node,
    struct vfs_stat *result
);

static enum vfs_stat_result
vfs_node_operations_test_stat_failure(
    struct vfs_node *node,
    struct vfs_stat *result
);

static enum vfs_stat_result
vfs_node_operations_test_stat_type_mismatch(
    struct vfs_node *node,
    struct vfs_stat *result
);

static void vfs_node_operations_test_open(void);

static void vfs_node_operations_test_open_validation(void);

static void vfs_node_operations_test_stat(void);

static void vfs_node_operations_test_stat_validation(void);

// Public functions implementations
void vfs_node_operations_test_run(void)
{
    vfs_node_operations_test_open();
    vfs_node_operations_test_open_validation();
    vfs_node_operations_test_stat();
    vfs_node_operations_test_stat_validation();

    diagnostics_write(
        "[vfs] Node open/stat contract tests passed\n"
    );
}

// Private functions and helpers implementations
static enum vfs_open_result
vfs_node_operations_test_open_success(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result)
{
    ++vfs_node_operations_test_open_call_count;

    if (
        node == NULL ||
        result == NULL ||
        vfs_node_operations_test_open_storage == NULL
    ) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (!vfs_file_initialize(
        vfs_node_operations_test_open_storage,
        node,
        access,
        NULL,
        NULL
    )) {
        return
            VFS_OPEN_RESULT_RESOURCE_EXHAUSTED;
    }

    *result =
        vfs_node_operations_test_open_storage;

    return
        VFS_OPEN_RESULT_OPENED;
}

static enum vfs_open_result
vfs_node_operations_test_open_denied(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result)
{
    (void) node;
    (void) access;

    ++vfs_node_operations_test_open_call_count;

    if (result != NULL) {
        *result =
            vfs_node_operations_test_open_storage;
    }

    return
        VFS_OPEN_RESULT_ACCESS_DENIED;
}

static enum vfs_open_result
vfs_node_operations_test_open_empty_success(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result)
{
    (void) node;
    (void) access;

    ++vfs_node_operations_test_open_call_count;

    if (result == NULL) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    *result =
        NULL;

    return
        VFS_OPEN_RESULT_OPENED;
}

static enum vfs_stat_result
vfs_node_operations_test_stat_success(
    struct vfs_node *node,
    struct vfs_stat *result)
{
    ++vfs_node_operations_test_stat_call_count;

    if (
        node == NULL ||
        result == NULL
    ) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    result->type =
        node->type;

    result->size =
        4096;

    return
        VFS_STAT_RESULT_SUCCESS;
}

static enum vfs_stat_result
vfs_node_operations_test_stat_failure(
    struct vfs_node *node,
    struct vfs_stat *result)
{
    (void) node;

    ++vfs_node_operations_test_stat_call_count;

    if (result != NULL) {
        result->type =
            VFS_NODE_TYPE_CHARACTER_DEVICE;

        result->size =
            UINT64_MAX;
    }

    return
        VFS_STAT_RESULT_NOT_SUPPORTED;
}

static enum vfs_stat_result
vfs_node_operations_test_stat_type_mismatch(
    struct vfs_node *node,
    struct vfs_stat *result)
{
    ++vfs_node_operations_test_stat_call_count;

    if (
        node == NULL ||
        result == NULL
    ) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    result->type =
        VFS_NODE_TYPE_DIRECTORY;

    result->size =
        8192;

    return
        VFS_STAT_RESULT_SUCCESS;
}

static void vfs_node_operations_test_open(void)
{
    struct vfs_node node;
    struct vfs_file file;

    struct vfs_node_operations operations = {
        .open =
            vfs_node_operations_test_open_success,
    };

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_REGULAR_FILE,
        &operations,
        NULL
    )) {
        kernel_panic(
            "VFS open fixture node initialization failed"
        );
    }

    vfs_node_operations_test_open_call_count =
        0;

    vfs_node_operations_test_open_storage =
        &file;

    struct vfs_file *result =
        NULL;

    enum vfs_open_result open_result =
        vfs_node_open(
            &node,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &result
        );

    if (
        open_result !=
            VFS_OPEN_RESULT_OPENED ||
        result != &file ||
        vfs_node_operations_test_open_call_count != 1 ||
        file.reference_count != 1 ||
        file.node != &node ||
        file.offset != 0 ||
        file.access !=
            (
                VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE
            ) ||
        node.reference_count != 2
    ) {
        kernel_panic(
            "VFS node open success contract failed"
        );
    }

    if (
        !vfs_file_release(&file) ||
        node.reference_count != 1 ||
        !vfs_node_release(&node)
    ) {
        kernel_panic(
            "VFS node open fixture cleanup failed"
        );
    }
}

static void vfs_node_operations_test_open_validation(void)
{
    struct vfs_node node;
    struct vfs_file file;
    struct vfs_file sentinel_file;

    struct vfs_node_operations success_operations = {
        .open =
            vfs_node_operations_test_open_success,
    };

    struct vfs_node_operations denied_operations = {
        .open =
            vfs_node_operations_test_open_denied,
    };

    struct vfs_node_operations empty_success_operations = {
        .open =
            vfs_node_operations_test_open_empty_success,
    };

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_REGULAR_FILE,
        &success_operations,
        NULL
    )) {
        kernel_panic(
            "VFS open validation fixture initialization failed"
        );
    }

    vfs_node_operations_test_open_storage =
        &file;

    vfs_node_operations_test_open_call_count =
        0;

    struct vfs_file *result =
        &sentinel_file;

    if (
        vfs_node_open(
            &node,
            0,
            &result
        ) != VFS_OPEN_RESULT_INVALID_ARGUMENT ||
        result != &sentinel_file ||
        vfs_node_operations_test_open_call_count != 0
    ) {
        kernel_panic(
            "VFS node open accepted empty access mode"
        );
    }

    enum vfs_open_access invalid_access =
        (enum vfs_open_access) (
            VFS_OPEN_ACCESS_READ |
            (1U << 2)
        );

    if (
        vfs_node_open(
            &node,
            invalid_access,
            &result
        ) != VFS_OPEN_RESULT_INVALID_ARGUMENT ||
        result != &sentinel_file ||
        vfs_node_operations_test_open_call_count != 0
    ) {
        kernel_panic(
            "VFS node open accepted unknown access bits"
        );
    }

    if (
        vfs_node_open(
            NULL,
            VFS_OPEN_ACCESS_READ,
            &result
        ) != VFS_OPEN_RESULT_INVALID_ARGUMENT ||
        result != &sentinel_file ||
        vfs_node_operations_test_open_call_count != 0
    ) {
        kernel_panic(
            "VFS node open accepted NULL node"
        );
    }

    if (
        vfs_node_open(
            &node,
            VFS_OPEN_ACCESS_READ,
            NULL
        ) != VFS_OPEN_RESULT_INVALID_ARGUMENT ||
        vfs_node_operations_test_open_call_count != 0
    ) {
        kernel_panic(
            "VFS node open accepted NULL result storage"
        );
    }

    node.operations =
        NULL;

    if (
        vfs_node_open(
            &node,
            VFS_OPEN_ACCESS_READ,
            &result
        ) != VFS_OPEN_RESULT_NOT_SUPPORTED ||
        result != &sentinel_file ||
        vfs_node_operations_test_open_call_count != 0
    ) {
        kernel_panic(
            "VFS node open accepted missing operation"
        );
    }

    node.operations =
        &success_operations;

    node.reference_count =
        0;

    if (
        vfs_node_open(
            &node,
            VFS_OPEN_ACCESS_READ,
            &result
        ) != VFS_OPEN_RESULT_INVALID_ARGUMENT ||
        result != &sentinel_file ||
        vfs_node_operations_test_open_call_count != 0
    ) {
        kernel_panic(
            "VFS node open accepted dead node"
        );
    }

    node.reference_count =
        1;

    node.operations =
        &denied_operations;

    if (
        vfs_node_open(
            &node,
            VFS_OPEN_ACCESS_READ,
            &result
        ) != VFS_OPEN_RESULT_ACCESS_DENIED ||
        result != &sentinel_file ||
        vfs_node_operations_test_open_call_count != 1
    ) {
        kernel_panic(
            "VFS node open failure published callback output"
        );
    }

    node.operations =
        &empty_success_operations;

    if (
        vfs_node_open(
            &node,
            VFS_OPEN_ACCESS_READ,
            &result
        ) != VFS_OPEN_RESULT_INVALID_ARGUMENT ||
        result != &sentinel_file ||
        vfs_node_operations_test_open_call_count != 2
    ) {
        kernel_panic(
            "VFS node open accepted empty successful result"
        );
    }

    if (!vfs_node_release(
        &node
    )) {
        kernel_panic(
            "VFS open validation fixture cleanup failed"
        );
    }
}

static void vfs_node_operations_test_stat(void)
{
    struct vfs_node node;

    struct vfs_node_operations operations = {
        .stat =
            vfs_node_operations_test_stat_success,
    };

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_REGULAR_FILE,
        &operations,
        NULL
    )) {
        kernel_panic(
            "VFS stat fixture node initialization failed"
        );
    }

    vfs_node_operations_test_stat_call_count =
        0;

    struct vfs_stat result = {
        .type =
            VFS_NODE_TYPE_DIRECTORY,
        .size =
            17,
    };

    if (
        vfs_node_stat(
            &node,
            &result
        ) != VFS_STAT_RESULT_SUCCESS ||
        result.type !=
            VFS_NODE_TYPE_REGULAR_FILE ||
        result.size != 4096 ||
        vfs_node_operations_test_stat_call_count != 1
    ) {
        kernel_panic(
            "VFS node stat success contract failed"
        );
    }

    if (!vfs_node_release(
        &node
    )) {
        kernel_panic(
            "VFS stat fixture cleanup failed"
        );
    }
}

static void vfs_node_operations_test_stat_validation(void)
{
    struct vfs_node node;

    struct vfs_node_operations success_operations = {
        .stat =
            vfs_node_operations_test_stat_success,
    };

    struct vfs_node_operations failure_operations = {
        .stat =
            vfs_node_operations_test_stat_failure,
    };

    struct vfs_node_operations mismatch_operations = {
        .stat =
            vfs_node_operations_test_stat_type_mismatch,
    };

    if (!vfs_node_initialize(
        &node,
        VFS_NODE_TYPE_REGULAR_FILE,
        &success_operations,
        NULL
    )) {
        kernel_panic(
            "VFS stat validation fixture initialization failed"
        );
    }

    vfs_node_operations_test_stat_call_count =
        0;

    struct vfs_stat result = {
        .type =
            VFS_NODE_TYPE_CHARACTER_DEVICE,
        .size =
            123,
    };

    if (
        vfs_node_stat(
            NULL,
            &result
        ) != VFS_STAT_RESULT_INVALID_ARGUMENT ||
        result.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        result.size != 123 ||
        vfs_node_operations_test_stat_call_count != 0
    ) {
        kernel_panic(
            "VFS node stat accepted NULL node"
        );
    }

    if (
        vfs_node_stat(
            &node,
            NULL
        ) != VFS_STAT_RESULT_INVALID_ARGUMENT ||
        vfs_node_operations_test_stat_call_count != 0
    ) {
        kernel_panic(
            "VFS node stat accepted NULL result storage"
        );
    }

    node.operations =
        NULL;

    if (
        vfs_node_stat(
            &node,
            &result
        ) != VFS_STAT_RESULT_NOT_SUPPORTED ||
        result.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        result.size != 123 ||
        vfs_node_operations_test_stat_call_count != 0
    ) {
        kernel_panic(
            "VFS node stat accepted missing operation"
        );
    }

    node.operations =
        &success_operations;

    node.reference_count =
        0;

    if (
        vfs_node_stat(
            &node,
            &result
        ) != VFS_STAT_RESULT_INVALID_ARGUMENT ||
        result.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        result.size != 123 ||
        vfs_node_operations_test_stat_call_count != 0
    ) {
        kernel_panic(
            "VFS node stat accepted dead node"
        );
    }

    node.reference_count =
        1;

    node.operations =
        &failure_operations;

    if (
        vfs_node_stat(
            &node,
            &result
        ) != VFS_STAT_RESULT_NOT_SUPPORTED ||
        result.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        result.size != 123 ||
        vfs_node_operations_test_stat_call_count != 1
    ) {
        kernel_panic(
            "VFS node stat failure published callback output"
        );
    }

    node.operations =
        &mismatch_operations;

    if (
        vfs_node_stat(
            &node,
            &result
        ) != VFS_STAT_RESULT_INVALID_ARGUMENT ||
        result.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        result.size != 123 ||
        vfs_node_operations_test_stat_call_count != 2
    ) {
        kernel_panic(
            "VFS node stat accepted mismatched object type"
        );
    }

    if (!vfs_node_release(
        &node
    )) {
        kernel_panic(
            "VFS stat validation fixture cleanup failed"
        );
    }
}

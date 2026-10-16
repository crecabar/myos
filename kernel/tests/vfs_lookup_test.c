// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file vfs_lookup_test.c
 * @brief VFS directory lookup contract regression tests.
 */

#include "vfs_lookup_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

// Private structures
struct vfs_lookup_test_entry {
    const char *name;
    size_t name_length;
    struct vfs_node *node;
};

struct vfs_lookup_test_directory {
    const struct vfs_lookup_test_entry *entries;
    size_t entry_count;
    size_t lookup_count;

    enum vfs_lookup_result callback_result;
};

// Private functions and helpers declarations
static enum vfs_lookup_result vfs_lookup_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result
);

static bool vfs_lookup_test_name_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
);

static void vfs_lookup_test_success(void);

static void vfs_lookup_test_errors(void);

static void vfs_lookup_test_reference_failure(void);

// Public functions implementations
void vfs_lookup_test_run(void)
{
    vfs_lookup_test_success();
    vfs_lookup_test_errors();
    vfs_lookup_test_reference_failure();

    diagnostics_write(
        "[vfs] Directory lookup contract tests passed\n"
    );
}

// Private functions and helpers implementations
static enum vfs_lookup_result vfs_lookup_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result)
{
    if (
        directory == NULL ||
        name == NULL ||
        result == NULL
    ) {
        kernel_panic(
            "VFS lookup test callback received invalid input"
        );
    }

    struct vfs_lookup_test_directory *fixture =
        directory->private_data;

    if (fixture == NULL) {
        kernel_panic(
            "VFS lookup test directory has no fixture"
        );
    }

    ++fixture->lookup_count;

    if (
        fixture->callback_result !=
        VFS_LOOKUP_RESULT_FOUND
    ) {
        return
            fixture->callback_result;
    }

    for (
        size_t index = 0;
        index < fixture->entry_count;
        ++index
    ) {
        const struct vfs_lookup_test_entry *entry =
            &fixture->entries[index];

        if (vfs_lookup_test_name_equal(
            name,
            name_length,
            entry->name,
            entry->name_length
        )) {
            *result =
                entry->node;

            return
                VFS_LOOKUP_RESULT_FOUND;
        }
    }

    return
        VFS_LOOKUP_RESULT_NOT_FOUND;
}

static bool vfs_lookup_test_name_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length)
{
    if (left_length != right_length) {
        return false;
    }

    for (size_t index = 0;
         index < left_length;
         ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }

    return true;
}

static void vfs_lookup_test_success(void)
{
    struct vfs_node root;
    struct vfs_node etc;
    struct vfs_node motd;

    struct vfs_lookup_test_entry root_entries[] = {
        {
            .name = "etc",
            .name_length = 3,
            .node = &etc,
        },
    };

    struct vfs_lookup_test_entry etc_entries[] = {
        {
            .name = "motd",
            .name_length = 4,
            .node = &motd,
        },
    };

    struct vfs_lookup_test_directory root_fixture = {
        .entries = root_entries,
        .entry_count = 1,
        .lookup_count = 0,
    };

    struct vfs_lookup_test_directory etc_fixture = {
        .entries = etc_entries,
        .entry_count = 1,
        .lookup_count = 0,
    };

    struct vfs_node_operations directory_operations = {
        .lookup =
            vfs_lookup_test_lookup,
        .destroy =
            NULL,
    };

    if (
        !vfs_node_initialize(
            &root,
            VFS_NODE_TYPE_DIRECTORY,
            &directory_operations,
            &root_fixture
        ) ||
        !vfs_node_initialize(
            &etc,
            VFS_NODE_TYPE_DIRECTORY,
            &directory_operations,
            &etc_fixture
        ) ||
        !vfs_node_initialize(
            &motd,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize VFS lookup fixture"
        );
    }

    struct vfs_node *etc_result =
        NULL;

    if (
        vfs_node_lookup(
            &root,
            "etc",
            3,
            &etc_result
        ) != VFS_LOOKUP_RESULT_FOUND ||
        etc_result != &etc ||
        etc.reference_count != 2 ||
        root_fixture.lookup_count != 1
    ) {
        kernel_panic(
            "VFS lookup did not return retained directory child"
        );
    }

    struct vfs_node *motd_result =
        NULL;

    if (
        vfs_node_lookup(
            etc_result,
            "motd",
            4,
            &motd_result
        ) != VFS_LOOKUP_RESULT_FOUND ||
        motd_result != &motd ||
        motd.reference_count != 2 ||
        etc_fixture.lookup_count != 1
    ) {
        kernel_panic(
            "VFS nested lookup did not return retained file child"
        );
    }

    if (
        !vfs_node_release(
            motd_result
        ) ||
        motd.reference_count != 1 ||
        !vfs_node_release(
            etc_result
        ) ||
        etc.reference_count != 1
    ) {
        kernel_panic(
            "VFS lookup result ownership cleanup failed"
        );
    }

    if (
        !vfs_node_release(&motd) ||
        !vfs_node_release(&etc) ||
        !vfs_node_release(&root)
    ) {
        kernel_panic(
            "VFS lookup fixture cleanup failed"
        );
    }
}

static void vfs_lookup_test_errors(void)
{
    struct vfs_node directory;
    struct vfs_node regular;

    struct vfs_lookup_test_directory fixture = {
        .entries = NULL,
        .entry_count = 0,
        .lookup_count = 0,
    };

    struct vfs_node_operations directory_operations = {
        .lookup =
            vfs_lookup_test_lookup,
        .destroy =
            NULL,
    };

    if (
        !vfs_node_initialize(
            &directory,
            VFS_NODE_TYPE_DIRECTORY,
            &directory_operations,
            &fixture
        ) ||
        !vfs_node_initialize(
            &regular,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize VFS lookup error fixture"
        );
    }

    struct vfs_node sentinel;

    if (!vfs_node_initialize(
        &sentinel,
        VFS_NODE_TYPE_REGULAR_FILE,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize VFS lookup sentinel"
        );
    }

    struct vfs_node *result =
        &sentinel;

    if (
        vfs_node_lookup(
            &directory,
            "missing",
            7,
            &result
        ) != VFS_LOOKUP_RESULT_NOT_FOUND ||
        result != &sentinel ||
        fixture.lookup_count != 1
    ) {
        kernel_panic(
            "VFS missing lookup contract failed"
        );
    }

    if (
        vfs_node_lookup(
            &regular,
            "child",
            5,
            &result
        ) != VFS_LOOKUP_RESULT_NOT_DIRECTORY ||
        result != &sentinel
    ) {
        kernel_panic(
            "VFS lookup accepted non-directory node"
        );
    }

    struct vfs_node unsupported_directory;

    if (!vfs_node_initialize(
        &unsupported_directory,
        VFS_NODE_TYPE_DIRECTORY,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize unsupported lookup directory"
        );
    }

    if (
        vfs_node_lookup(
            &unsupported_directory,
            "child",
            5,
            &result
        ) != VFS_LOOKUP_RESULT_NOT_SUPPORTED ||
        result != &sentinel
    ) {
        kernel_panic(
            "VFS unsupported lookup contract failed"
        );
    }

    const char embedded_nul[] = {
        'a',
        '\0',
        'b',
    };

    const char slash_name[] = {
        'a',
        '/',
        'b',
    };

    if (
        vfs_node_lookup(
            NULL,
            "x",
            1,
            &result
        ) != VFS_LOOKUP_RESULT_INVALID_ARGUMENT ||
        vfs_node_lookup(
            &directory,
            NULL,
            1,
            &result
        ) != VFS_LOOKUP_RESULT_INVALID_ARGUMENT ||
        vfs_node_lookup(
            &directory,
            "x",
            1,
            NULL
        ) != VFS_LOOKUP_RESULT_INVALID_ARGUMENT ||
        vfs_node_lookup(
            &directory,
            "",
            0,
            &result
        ) != VFS_LOOKUP_RESULT_INVALID_ARGUMENT ||
        vfs_node_lookup(
            &directory,
            ".",
            1,
            &result
        ) != VFS_LOOKUP_RESULT_INVALID_ARGUMENT ||
        vfs_node_lookup(
            &directory,
            "..",
            2,
            &result
        ) != VFS_LOOKUP_RESULT_INVALID_ARGUMENT ||
        vfs_node_lookup(
            &directory,
            embedded_nul,
            sizeof(embedded_nul),
            &result
        ) != VFS_LOOKUP_RESULT_INVALID_ARGUMENT ||
        vfs_node_lookup(
            &directory,
            slash_name,
            sizeof(slash_name),
            &result
        ) != VFS_LOOKUP_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "VFS lookup accepted invalid component"
        );
    }

    if (
        result != &sentinel ||
        fixture.lookup_count != 1
    ) {
        kernel_panic(
            "Rejected VFS lookup mutated observable state"
        );
    }

    /*
     * Filesystem-originated failures must propagate through the generic VFS
     * without being collapsed into NOT_FOUND and without publishing a result.
     */
    fixture.callback_result =
        VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED;

    if (
        vfs_node_lookup(
            &directory,
            "dynamic",
            7,
            &result
        ) !=
            VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED ||
        result !=
            &sentinel ||
        fixture.lookup_count != 2
    ) {
        kernel_panic(
            "VFS filesystem lookup failure propagation failed"
        );
    }

    fixture.callback_result =
        VFS_LOOKUP_RESULT_FOUND;

    if (
        !vfs_node_release(
            &unsupported_directory
        ) ||
        !vfs_node_release(
            &sentinel
        ) ||
        !vfs_node_release(
            &regular
        ) ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "VFS lookup error fixture cleanup failed"
        );
    }
}

static void vfs_lookup_test_reference_failure(void)
{
    struct vfs_node directory;
    struct vfs_node child;
    struct vfs_node sentinel;

    struct vfs_lookup_test_entry entries[] = {
        {
            .name = "child",
            .name_length = 5,
            .node = &child,
        },
    };

    struct vfs_lookup_test_directory fixture = {
        .entries = entries,
        .entry_count = 1,
        .lookup_count = 0,
    };

    struct vfs_node_operations operations = {
        .lookup =
            vfs_lookup_test_lookup,
        .destroy =
            NULL,
    };

    if (
        !vfs_node_initialize(
            &directory,
            VFS_NODE_TYPE_DIRECTORY,
            &operations,
            &fixture
        ) ||
        !vfs_node_initialize(
            &child,
            VFS_NODE_TYPE_REGULAR_FILE,
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
            "Unable to initialize VFS lookup reference fixture"
        );
    }

    child.reference_count =
        SIZE_MAX;

    struct vfs_node *result =
        &sentinel;

    if (
        vfs_node_lookup(
            &directory,
            "child",
            5,
            &result
        ) != VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED ||
        result != &sentinel ||
        child.reference_count != SIZE_MAX ||
        fixture.lookup_count != 1
    ) {
        kernel_panic(
            "VFS lookup reference failure contract failed"
        );
    }

    child.reference_count =
        1;

    if (
        !vfs_node_release(
            &sentinel
        ) ||
        !vfs_node_release(
            &child
        ) ||
        !vfs_node_release(
            &directory
        )
    ) {
        kernel_panic(
            "VFS lookup reference fixture cleanup failed"
        );
    }
}

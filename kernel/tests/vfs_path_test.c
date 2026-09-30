// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file vfs_path_test.c
 * @brief VFS pathname resolution regression tests.
 */

#include "vfs_path_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../vfs/path.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

struct vfs_path_test_entry {
    const char *name;
    size_t name_length;
    struct vfs_node *node;
};

struct vfs_path_test_directory {
    struct vfs_node *parent;

    const struct vfs_path_test_entry *entries;
    size_t entry_count;

    size_t lookup_count;
    size_t parent_count;
};

struct vfs_path_test_tree {
    struct vfs_node root;
    struct vfs_node etc;
    struct vfs_node bin;
    struct vfs_node motd;
    struct vfs_node hello;
    struct vfs_node file;
    struct vfs_node plain;
    struct vfs_node sentinel;

    struct vfs_path_test_entry root_entries[4];
    struct vfs_path_test_entry etc_entries[1];
    struct vfs_path_test_entry bin_entries[1];

    struct vfs_path_test_directory root_directory;
    struct vfs_path_test_directory etc_directory;
    struct vfs_path_test_directory bin_directory;
};

static struct vfs_node *vfs_path_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length
);

static struct vfs_node *vfs_path_test_parent(
    struct vfs_node *directory
);

static bool vfs_path_test_name_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
);

static void vfs_path_test_tree_initialize(
    struct vfs_path_test_tree *tree
);

static void vfs_path_test_tree_expect_clean(
    const struct vfs_path_test_tree *tree
);

static void vfs_path_test_tree_release(
    struct vfs_path_test_tree *tree
);

static void vfs_path_test_expect_success(
    struct vfs_path_test_tree *tree,
    struct vfs_node *start,
    const char *path,
    size_t path_length,
    struct vfs_node *expected
);

static void vfs_path_test_expect_failure(
    struct vfs_path_test_tree *tree,
    struct vfs_node *start,
    const char *path,
    size_t path_length,
    enum vfs_path_result expected
);

static void vfs_path_test_success(void);

static void vfs_path_test_errors(void);

static void vfs_path_test_reference_failures(void);

static const struct vfs_node_operations
    vfs_path_test_directory_operations = {
        .lookup =
            vfs_path_test_lookup,
        .parent =
            vfs_path_test_parent,
        .destroy =
            NULL,
    };

void vfs_path_test_run(void)
{
    vfs_path_test_success();
    vfs_path_test_errors();
    vfs_path_test_reference_failures();

    diagnostics_write(
        "[vfs] Pathname resolution tests passed\n"
    );
}

static struct vfs_node *vfs_path_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length)
{
    if (
        directory == NULL ||
        name == NULL
    ) {
        kernel_panic(
            "VFS path lookup fixture received invalid input"
        );
    }

    struct vfs_path_test_directory *fixture =
        directory->private_data;

    if (fixture == NULL) {
        kernel_panic(
            "VFS path lookup directory has no fixture"
        );
    }

    ++fixture->lookup_count;

    for (size_t index = 0;
         index < fixture->entry_count;
         ++index) {
        const struct vfs_path_test_entry *entry =
            &fixture->entries[index];

        if (vfs_path_test_name_equal(
            name,
            name_length,
            entry->name,
            entry->name_length
        )) {
            return
                entry->node;
        }
    }

    return NULL;
}

static struct vfs_node *vfs_path_test_parent(
    struct vfs_node *directory)
{
    if (directory == NULL) {
        kernel_panic(
            "VFS path parent fixture received NULL"
        );
    }

    struct vfs_path_test_directory *fixture =
        directory->private_data;

    if (fixture == NULL) {
        kernel_panic(
            "VFS path parent directory has no fixture"
        );
    }

    ++fixture->parent_count;

    return
        fixture->parent;
}

static bool vfs_path_test_name_equal(
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

static void vfs_path_test_tree_initialize(
    struct vfs_path_test_tree *tree)
{
    if (tree == NULL) {
        kernel_panic(
            "Unable to initialize NULL VFS path tree"
        );
    }

    tree->root_entries[0] =
        (struct vfs_path_test_entry) {
            .name = "etc",
            .name_length = 3,
            .node = &tree->etc,
        };

    tree->root_entries[1] =
        (struct vfs_path_test_entry) {
            .name = "bin",
            .name_length = 3,
            .node = &tree->bin,
        };

    tree->root_entries[2] =
        (struct vfs_path_test_entry) {
            .name = "file",
            .name_length = 4,
            .node = &tree->file,
        };

    tree->root_entries[3] =
        (struct vfs_path_test_entry) {
            .name = "plain",
            .name_length = 5,
            .node = &tree->plain,
        };

    tree->etc_entries[0] =
        (struct vfs_path_test_entry) {
            .name = "motd",
            .name_length = 4,
            .node = &tree->motd,
        };

    tree->bin_entries[0] =
        (struct vfs_path_test_entry) {
            .name = "hello",
            .name_length = 5,
            .node = &tree->hello,
        };

    tree->root_directory =
        (struct vfs_path_test_directory) {
            .parent = NULL,
            .entries = tree->root_entries,
            .entry_count = 4,
            .lookup_count = 0,
            .parent_count = 0,
        };

    tree->etc_directory =
        (struct vfs_path_test_directory) {
            .parent = &tree->root,
            .entries = tree->etc_entries,
            .entry_count = 1,
            .lookup_count = 0,
            .parent_count = 0,
        };

    tree->bin_directory =
        (struct vfs_path_test_directory) {
            .parent = &tree->root,
            .entries = tree->bin_entries,
            .entry_count = 1,
            .lookup_count = 0,
            .parent_count = 0,
        };

    if (
        !vfs_node_initialize(
            &tree->root,
            VFS_NODE_TYPE_DIRECTORY,
            &vfs_path_test_directory_operations,
            &tree->root_directory
        ) ||
        !vfs_node_initialize(
            &tree->etc,
            VFS_NODE_TYPE_DIRECTORY,
            &vfs_path_test_directory_operations,
            &tree->etc_directory
        ) ||
        !vfs_node_initialize(
            &tree->bin,
            VFS_NODE_TYPE_DIRECTORY,
            &vfs_path_test_directory_operations,
            &tree->bin_directory
        ) ||
        !vfs_node_initialize(
            &tree->motd,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &tree->hello,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &tree->file,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &tree->plain,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &tree->sentinel,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize VFS pathname fixture"
        );
    }
}

static void vfs_path_test_tree_expect_clean(
    const struct vfs_path_test_tree *tree)
{
    if (
        tree->root.reference_count != 1 ||
        tree->etc.reference_count != 1 ||
        tree->bin.reference_count != 1 ||
        tree->motd.reference_count != 1 ||
        tree->hello.reference_count != 1 ||
        tree->file.reference_count != 1 ||
        tree->plain.reference_count != 1 ||
        tree->sentinel.reference_count != 1
    ) {
        kernel_panic(
            "VFS pathname resolution leaked node references"
        );
    }
}

static void vfs_path_test_tree_release(
    struct vfs_path_test_tree *tree)
{
    vfs_path_test_tree_expect_clean(
        tree
    );

    if (
        !vfs_node_release(
            &tree->sentinel
        ) ||
        !vfs_node_release(
            &tree->plain
        ) ||
        !vfs_node_release(
            &tree->file
        ) ||
        !vfs_node_release(
            &tree->hello
        ) ||
        !vfs_node_release(
            &tree->motd
        ) ||
        !vfs_node_release(
            &tree->bin
        ) ||
        !vfs_node_release(
            &tree->etc
        ) ||
        !vfs_node_release(
            &tree->root
        )
    ) {
        kernel_panic(
            "VFS pathname fixture cleanup failed"
        );
    }
}

static void vfs_path_test_expect_success(
    struct vfs_path_test_tree *tree,
    struct vfs_node *start,
    const char *path,
    size_t path_length,
    struct vfs_node *expected)
{
    struct vfs_node *result =
        NULL;

    if (
        vfs_path_resolve(
            &tree->root,
            start,
            path,
            path_length,
            &result
        ) != VFS_PATH_RESULT_FOUND ||
        result != expected ||
        expected->reference_count != 2
    ) {
        kernel_panic(
            "VFS pathname did not resolve expected node"
        );
    }

    if (!vfs_node_release(
        result
    )) {
        kernel_panic(
            "Unable to release VFS pathname result"
        );
    }

    vfs_path_test_tree_expect_clean(
        tree
    );
}

static void vfs_path_test_expect_failure(
    struct vfs_path_test_tree *tree,
    struct vfs_node *start,
    const char *path,
    size_t path_length,
    enum vfs_path_result expected)
{
    struct vfs_node *result =
        &tree->sentinel;

    if (
        vfs_path_resolve(
            &tree->root,
            start,
            path,
            path_length,
            &result
        ) != expected ||
        result != &tree->sentinel
    ) {
        kernel_panic(
            "VFS pathname returned incorrect failure result"
        );
    }

    vfs_path_test_tree_expect_clean(
        tree
    );
}

static void vfs_path_test_success(void)
{
    struct vfs_path_test_tree tree;

    vfs_path_test_tree_initialize(
        &tree
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.root,
        "/",
        1,
        &tree.root
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.etc,
        "/bin",
        4,
        &tree.bin
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.root,
        "/etc/motd",
        9,
        &tree.motd
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.etc,
        "motd",
        4,
        &tree.motd
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.root,
        "/etc//motd",
        10,
        &tree.motd
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.root,
        "/etc/./motd",
        11,
        &tree.motd
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.root,
        "/etc/../bin/hello",
        17,
        &tree.hello
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.root,
        "/../../etc/motd",
        15,
        &tree.motd
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.etc,
        ".",
        1,
        &tree.etc
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.etc,
        "..",
        2,
        &tree.root
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.etc,
        "../bin",
        6,
        &tree.bin
    );

    vfs_path_test_expect_success(
        &tree,
        &tree.root,
        "////",
        4,
        &tree.root
    );

    /*
     * A directory without lookup/parent operations can still be the final
     * successfully resolved object.
     */
    vfs_path_test_expect_success(
        &tree,
        &tree.root,
        "/plain/",
        7,
        &tree.plain
    );

    vfs_path_test_tree_release(
        &tree
    );
}

static void vfs_path_test_errors(void)
{
    struct vfs_path_test_tree tree;

    vfs_path_test_tree_initialize(
        &tree
    );

    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        "/missing",
        8,
        VFS_PATH_RESULT_NOT_FOUND
    );

    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        "/etc/missing",
        12,
        VFS_PATH_RESULT_NOT_FOUND
    );

    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        "/etc/../missing",
        15,
        VFS_PATH_RESULT_NOT_FOUND
    );

    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        "/file/child",
        11,
        VFS_PATH_RESULT_NOT_DIRECTORY
    );

    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        "/file/",
        6,
        VFS_PATH_RESULT_NOT_DIRECTORY
    );

    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        "file/.",
        6,
        VFS_PATH_RESULT_NOT_DIRECTORY
    );

    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        "/plain/child",
        12,
        VFS_PATH_RESULT_NOT_SUPPORTED
    );

    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        "/plain/..",
        9,
        VFS_PATH_RESULT_NOT_SUPPORTED
    );

    char oversized_component[
        VFS_NAME_MAX + 1
    ];

    for (size_t index = 0;
         index < sizeof(oversized_component);
         ++index) {
        oversized_component[index] =
            'a';
    }

    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        oversized_component,
        sizeof(oversized_component),
        VFS_PATH_RESULT_INVALID_ARGUMENT
    );

    const char embedded_nul[] = {
        'e',
        '\0',
        'c',
    };

    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        embedded_nul,
        sizeof(embedded_nul),
        VFS_PATH_RESULT_INVALID_ARGUMENT
    );

    /*
     * Oversized path length is rejected before the path bytes are inspected.
     */
    vfs_path_test_expect_failure(
        &tree,
        &tree.root,
        "x",
        VFS_PATH_MAX + 1,
        VFS_PATH_RESULT_INVALID_ARGUMENT
    );

    struct vfs_node *result =
        &tree.sentinel;

    if (
        vfs_path_resolve(
            &tree.root,
            &tree.root,
            "",
            0,
            &result
        ) != VFS_PATH_RESULT_INVALID_ARGUMENT ||
        result != &tree.sentinel ||
        vfs_path_resolve(
            NULL,
            &tree.root,
            "/",
            1,
            &result
        ) != VFS_PATH_RESULT_INVALID_ARGUMENT ||
        result != &tree.sentinel ||
        vfs_path_resolve(
            &tree.root,
            NULL,
            "/",
            1,
            &result
        ) != VFS_PATH_RESULT_INVALID_ARGUMENT ||
        result != &tree.sentinel ||
        vfs_path_resolve(
            &tree.root,
            &tree.root,
            NULL,
            1,
            &result
        ) != VFS_PATH_RESULT_INVALID_ARGUMENT ||
        result != &tree.sentinel ||
        vfs_path_resolve(
            &tree.root,
            &tree.root,
            "/",
            1,
            NULL
        ) != VFS_PATH_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "VFS pathname accepted invalid arguments"
        );
    }

    vfs_path_test_tree_expect_clean(
        &tree
    );

    vfs_path_test_tree_release(
        &tree
    );
}

static void vfs_path_test_reference_failures(void)
{
    struct vfs_path_test_tree tree;

    vfs_path_test_tree_initialize(
        &tree
    );

    /*
     * Descending lookup succeeds at the filesystem layer but the generic VFS
     * cannot acquire the child reference.
     */
    tree.motd.reference_count =
        SIZE_MAX;

    struct vfs_node *result =
        &tree.sentinel;

    if (
        vfs_path_resolve(
            &tree.root,
            &tree.root,
            "/etc/motd",
            9,
            &result
        ) != VFS_PATH_RESULT_RESOURCE_EXHAUSTED ||
        result != &tree.sentinel ||
        tree.root.reference_count != 1 ||
        tree.etc.reference_count != 1 ||
        tree.motd.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "VFS pathname lookup reference failure leaked ownership"
        );
    }

    tree.motd.reference_count =
        1;

    vfs_path_test_tree_expect_clean(
        &tree
    );

    /*
     * Parent traversal finds root but cannot retain it. The temporary current
     * reference to etc must still be released.
     */
    tree.root.reference_count =
        SIZE_MAX;

    result =
        &tree.sentinel;

    if (
        vfs_path_resolve(
            &tree.root,
            &tree.etc,
            "..",
            2,
            &result
        ) != VFS_PATH_RESULT_RESOURCE_EXHAUSTED ||
        result != &tree.sentinel ||
        tree.root.reference_count != SIZE_MAX ||
        tree.etc.reference_count != 1
    ) {
        kernel_panic(
            "VFS pathname parent reference failure leaked ownership"
        );
    }

    tree.root.reference_count =
        1;

    vfs_path_test_tree_expect_clean(
        &tree
    );

    /*
     * Initial traversal ownership itself can also fail before any component
     * is inspected.
     */
    tree.root.reference_count =
        SIZE_MAX;

    result =
        &tree.sentinel;

    if (
        vfs_path_resolve(
            &tree.root,
            &tree.root,
            "/",
            1,
            &result
        ) != VFS_PATH_RESULT_RESOURCE_EXHAUSTED ||
        result != &tree.sentinel ||
        tree.root.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "VFS pathname initial reference failure contract failed"
        );
    }

    tree.root.reference_count =
        1;

    vfs_path_test_tree_expect_clean(
        &tree
    );

    vfs_path_test_tree_release(
        &tree
    );
}

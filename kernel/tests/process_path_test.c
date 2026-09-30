// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_path_test.c
 * @brief Process-relative pathname and chdir regression tests.
 */

#include "process_path_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/cwd.h"
#include "../process/path.h"
#include "../vfs/vfs.h"
#include "../vfs/path.h"

#include <stddef.h>
#include <stdint.h>

struct process_path_test_entry {
    const char *name;
    size_t name_length;
    struct vfs_node *node;
};

struct process_path_test_directory {
    struct vfs_node *parent;
    const struct process_path_test_entry *entries;
    size_t entry_count;
};

struct process_path_test_tree {
    struct vfs_node root;
    struct vfs_node home;
    struct vfs_node docs;
    struct vfs_node etc;
    struct vfs_node motd;
    struct vfs_node file;
    struct vfs_node sentinel;

    struct process_path_test_entry root_entries[3];
    struct process_path_test_entry home_entries[1];
    struct process_path_test_entry etc_entries[1];

    struct process_path_test_directory root_directory;
    struct process_path_test_directory home_directory;
    struct process_path_test_directory docs_directory;
    struct process_path_test_directory etc_directory;
};

static struct vfs_node *process_path_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length
);

static struct vfs_node *process_path_test_parent(
    struct vfs_node *directory
);

static bool process_path_test_name_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
);

static void process_path_test_tree_initialize(
    struct process_path_test_tree *tree
);

static void process_path_test_expect_steady_refs(
    const struct process_path_test_tree *tree,
    const struct process_instance *instance
);

static void process_path_test_tree_release(
    struct process_path_test_tree *tree
);

static void process_path_test_expect_resolved(
    struct process_path_test_tree *tree,
    struct process_instance *instance,
    const char *path,
    size_t path_length,
    struct vfs_node *expected
);

static void process_path_test_expect_failure(
    struct process_path_test_tree *tree,
    struct process_instance *instance,
    const char *path,
    size_t path_length,
    enum process_path_result expected
);

static void process_path_test_resolution(void);

static void process_path_test_chdir(void);

static void process_path_test_errors(void);

static const struct vfs_node_operations
    process_path_test_directory_operations = {
        .lookup =
            process_path_test_lookup,
        .parent =
            process_path_test_parent,
        .destroy =
            NULL,
    };

void process_path_test_run(void)
{
    process_path_test_resolution();
    process_path_test_chdir();
    process_path_test_errors();

    diagnostics_write(
        "[process] Pathname context and chdir tests passed\n"
    );
}

static struct vfs_node *process_path_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length)
{
    if (
        directory == NULL ||
        name == NULL
    ) {
        kernel_panic(
            "Process path lookup fixture received invalid input"
        );
    }

    struct process_path_test_directory *fixture =
        directory->private_data;

    if (fixture == NULL) {
        kernel_panic(
            "Process path directory has no fixture"
        );
    }

    for (size_t index = 0;
         index < fixture->entry_count;
         ++index) {
        const struct process_path_test_entry *entry =
            &fixture->entries[index];

        if (process_path_test_name_equal(
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

static struct vfs_node *process_path_test_parent(
    struct vfs_node *directory)
{
    if (directory == NULL) {
        kernel_panic(
            "Process path parent fixture received NULL"
        );
    }

    struct process_path_test_directory *fixture =
        directory->private_data;

    if (fixture == NULL) {
        kernel_panic(
            "Process path directory has no parent fixture"
        );
    }

    return
        fixture->parent;
}

static bool process_path_test_name_equal(
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

static void process_path_test_tree_initialize(
    struct process_path_test_tree *tree)
{
    if (tree == NULL) {
        kernel_panic(
            "Unable to initialize NULL process path tree"
        );
    }

    tree->root_entries[0] =
        (struct process_path_test_entry) {
            .name = "home",
            .name_length = 4,
            .node = &tree->home,
        };

    tree->root_entries[1] =
        (struct process_path_test_entry) {
            .name = "etc",
            .name_length = 3,
            .node = &tree->etc,
        };

    tree->root_entries[2] =
        (struct process_path_test_entry) {
            .name = "file",
            .name_length = 4,
            .node = &tree->file,
        };

    tree->home_entries[0] =
        (struct process_path_test_entry) {
            .name = "docs",
            .name_length = 4,
            .node = &tree->docs,
        };

    tree->etc_entries[0] =
        (struct process_path_test_entry) {
            .name = "motd",
            .name_length = 4,
            .node = &tree->motd,
        };

    tree->root_directory =
        (struct process_path_test_directory) {
            .parent = NULL,
            .entries = tree->root_entries,
            .entry_count = 3,
        };

    tree->home_directory =
        (struct process_path_test_directory) {
            .parent = &tree->root,
            .entries = tree->home_entries,
            .entry_count = 1,
        };

    tree->docs_directory =
        (struct process_path_test_directory) {
            .parent = &tree->home,
            .entries = NULL,
            .entry_count = 0,
        };

    tree->etc_directory =
        (struct process_path_test_directory) {
            .parent = &tree->root,
            .entries = tree->etc_entries,
            .entry_count = 1,
        };

    if (
        !vfs_node_initialize(
            &tree->root,
            VFS_NODE_TYPE_DIRECTORY,
            &process_path_test_directory_operations,
            &tree->root_directory
        ) ||
        !vfs_node_initialize(
            &tree->home,
            VFS_NODE_TYPE_DIRECTORY,
            &process_path_test_directory_operations,
            &tree->home_directory
        ) ||
        !vfs_node_initialize(
            &tree->docs,
            VFS_NODE_TYPE_DIRECTORY,
            &process_path_test_directory_operations,
            &tree->docs_directory
        ) ||
        !vfs_node_initialize(
            &tree->etc,
            VFS_NODE_TYPE_DIRECTORY,
            &process_path_test_directory_operations,
            &tree->etc_directory
        ) ||
        !vfs_node_initialize(
            &tree->motd,
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
            &tree->sentinel,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize process pathname fixture"
        );
    }
}

static void process_path_test_expect_steady_refs(
    const struct process_path_test_tree *tree,
    const struct process_instance *instance)
{
    const struct vfs_node *cwd =
        process_cwd_get(
            instance
        );

    if (
        tree->root.reference_count !=
            (cwd == &tree->root ? 2U : 1U) ||
        tree->home.reference_count !=
            (cwd == &tree->home ? 2U : 1U) ||
        tree->docs.reference_count !=
            (cwd == &tree->docs ? 2U : 1U) ||
        tree->etc.reference_count !=
            (cwd == &tree->etc ? 2U : 1U) ||
        tree->motd.reference_count != 1 ||
        tree->file.reference_count != 1 ||
        tree->sentinel.reference_count != 1
    ) {
        kernel_panic(
            "Process pathname operation leaked VFS references"
        );
    }
}

static void process_path_test_tree_release(
    struct process_path_test_tree *tree)
{
    if (
        !vfs_node_release(
            &tree->sentinel
        ) ||
        !vfs_node_release(
            &tree->file
        ) ||
        !vfs_node_release(
            &tree->motd
        ) ||
        !vfs_node_release(
            &tree->etc
        ) ||
        !vfs_node_release(
            &tree->docs
        ) ||
        !vfs_node_release(
            &tree->home
        ) ||
        !vfs_node_release(
            &tree->root
        )
    ) {
        kernel_panic(
            "Process pathname fixture cleanup failed"
        );
    }
}

static void process_path_test_expect_resolved(
    struct process_path_test_tree *tree,
    struct process_instance *instance,
    const char *path,
    size_t path_length,
    struct vfs_node *expected)
{
    struct vfs_node *result =
        &tree->sentinel;

    size_t expected_before =
        expected->reference_count;

    if (
        process_path_resolve(
            instance,
            &tree->root,
            path,
            path_length,
            &result
        ) != PROCESS_PATH_RESULT_RESOLVED ||
        result != expected ||
        expected->reference_count !=
            expected_before + 1
    ) {
        kernel_panic(
            "Process pathname did not resolve expected node"
        );
    }

    if (!vfs_node_release(
        result
    )) {
        kernel_panic(
            "Unable to release process pathname result"
        );
    }

    process_path_test_expect_steady_refs(
        tree,
        instance
    );
}

static void process_path_test_expect_failure(
    struct process_path_test_tree *tree,
    struct process_instance *instance,
    const char *path,
    size_t path_length,
    enum process_path_result expected)
{
    struct vfs_node *result =
        &tree->sentinel;

    if (
        process_path_resolve(
            instance,
            &tree->root,
            path,
            path_length,
            &result
        ) != expected ||
        result != &tree->sentinel
    ) {
        kernel_panic(
            "Process pathname returned incorrect failure"
        );
    }

    process_path_test_expect_steady_refs(
        tree,
        instance
    );
}

static void process_path_test_resolution(void)
{
    struct process_path_test_tree tree;
    struct process_instance instance = {0};

    process_path_test_tree_initialize(
        &tree
    );

    /*
     * Absolute resolution does not require a current directory.
     */
    process_path_test_expect_resolved(
        &tree,
        &instance,
        "/etc",
        sizeof("/etc") - 1,
        &tree.etc
    );

    process_path_test_expect_failure(
        &tree,
        &instance,
        "etc",
        sizeof("etc") - 1,
        PROCESS_PATH_RESULT_NO_CURRENT_DIRECTORY
    );

    if (!process_cwd_set(
        &instance,
        &tree.home
    )) {
        kernel_panic(
            "Unable to initialize process pathname CWD"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    process_path_test_expect_resolved(
        &tree,
        &instance,
        "docs",
        sizeof("docs") - 1,
        &tree.docs
    );

    process_path_test_expect_resolved(
        &tree,
        &instance,
        "../etc",
        sizeof("../etc") - 1,
        &tree.etc
    );

    process_path_test_expect_resolved(
        &tree,
        &instance,
        "/etc/motd",
        sizeof("/etc/motd") - 1,
        &tree.motd
    );

    process_path_test_expect_failure(
        &tree,
        &instance,
        "/missing",
        sizeof("/missing") - 1,
        PROCESS_PATH_RESULT_NOT_FOUND
    );

    if (!process_cwd_release(
        &instance
    )) {
        kernel_panic(
            "Unable to release process pathname CWD"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    process_path_test_tree_release(
        &tree
    );
}

static void process_path_test_chdir(void)
{
    struct process_path_test_tree tree;
    struct process_instance instance = {0};

    process_path_test_tree_initialize(
        &tree
    );

    /*
     * An absolute chdir can establish the first CWD of a fresh process.
     */
    if (
        process_chdir(
            &instance,
            &tree.root,
            "/home",
            sizeof("/home") - 1
        ) != PROCESS_PATH_RESULT_RESOLVED ||
        process_cwd_get(
            &instance
        ) != &tree.home
    ) {
        kernel_panic(
            "Absolute chdir did not establish process CWD"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    if (
        process_chdir(
            &instance,
            &tree.root,
            "docs",
            sizeof("docs") - 1
        ) != PROCESS_PATH_RESULT_RESOLVED ||
        process_cwd_get(
            &instance
        ) != &tree.docs
    ) {
        kernel_panic(
            "Relative chdir did not update process CWD"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    if (
        process_chdir(
            &instance,
            &tree.root,
            "/etc",
            sizeof("/etc") - 1
        ) != PROCESS_PATH_RESULT_RESOLVED ||
        process_cwd_get(
            &instance
        ) != &tree.etc
    ) {
        kernel_panic(
            "Absolute chdir did not replace process CWD"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    /*
     * Failed chdir must leave the old CWD untouched.
     */
    if (
        process_chdir(
            &instance,
            &tree.root,
            "/etc/motd",
            sizeof("/etc/motd") - 1
        ) != PROCESS_PATH_RESULT_NOT_DIRECTORY ||
        process_cwd_get(
            &instance
        ) != &tree.etc
    ) {
        kernel_panic(
            "chdir to regular file mutated process CWD"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    if (
        process_chdir(
            &instance,
            &tree.root,
            "/missing",
            sizeof("/missing") - 1
        ) != PROCESS_PATH_RESULT_NOT_FOUND ||
        process_cwd_get(
            &instance
        ) != &tree.etc
    ) {
        kernel_panic(
            "Failed chdir mutated process CWD"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    /*
     * Re-selecting the current directory exercises the same-node ownership
     * path: resolver temporary reference + process-owned reference.
     */
    if (
        process_chdir(
            &instance,
            &tree.root,
            ".",
            sizeof(".") - 1
        ) != PROCESS_PATH_RESULT_RESOLVED ||
        process_cwd_get(
            &instance
        ) != &tree.etc
    ) {
        kernel_panic(
            "chdir to current directory changed identity"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    if (
        process_chdir(
            &instance,
            &tree.root,
            "..",
            sizeof("..") - 1
        ) != PROCESS_PATH_RESULT_RESOLVED ||
        process_cwd_get(
            &instance
        ) != &tree.root
    ) {
        kernel_panic(
            "chdir parent traversal failed"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    if (!process_cwd_release(
        &instance
    )) {
        kernel_panic(
            "Unable to release chdir test CWD"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    process_path_test_tree_release(
        &tree
    );
}

static void process_path_test_errors(void)
{
    struct process_path_test_tree tree;
    struct process_instance instance = {0};

    process_path_test_tree_initialize(
        &tree
    );

    struct vfs_node *result =
        &tree.sentinel;

    if (
        process_path_resolve(
            NULL,
            &tree.root,
            "/",
            sizeof("/") - 1,
            &result
        ) != PROCESS_PATH_RESULT_INVALID_ARGUMENT ||
        result != &tree.sentinel ||
        process_path_resolve(
            &instance,
            NULL,
            "/",
            sizeof("/") - 1,
            &result
        ) != PROCESS_PATH_RESULT_INVALID_ARGUMENT ||
        result != &tree.sentinel ||
        process_path_resolve(
            &instance,
            &tree.root,
            NULL,
            1,
            &result
        ) != PROCESS_PATH_RESULT_INVALID_ARGUMENT ||
        result != &tree.sentinel ||
        process_path_resolve(
            &instance,
            &tree.root,
            "",
            0,
            &result
        ) != PROCESS_PATH_RESULT_INVALID_ARGUMENT ||
        result != &tree.sentinel ||
        process_path_resolve(
            &instance,
            &tree.root,
            "/",
            sizeof("/") - 1,
            NULL
        ) != PROCESS_PATH_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Process pathname accepted invalid arguments"
        );
    }

    const char embedded_nul[] = {
        '/',
        'e',
        '\0',
        'c',
    };

    process_path_test_expect_failure(
        &tree,
        &instance,
        embedded_nul,
        sizeof(embedded_nul),
        PROCESS_PATH_RESULT_INVALID_ARGUMENT
    );

    process_path_test_expect_failure(
        &tree,
        &instance,
        "x",
        VFS_PATH_MAX + 1,
        PROCESS_PATH_RESULT_INVALID_ARGUMENT
    );

    /*
     * Failure to acquire the namespace root reference must propagate without
     * publishing a result.
     */
    tree.root.reference_count =
        SIZE_MAX;

    result =
        &tree.sentinel;

    if (
        process_path_resolve(
            &instance,
            &tree.root,
            "/",
            sizeof("/") - 1,
            &result
        ) != PROCESS_PATH_RESULT_RESOURCE_EXHAUSTED ||
        result != &tree.sentinel ||
        tree.root.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "Process pathname root reference exhaustion contract failed"
        );
    }

    tree.root.reference_count =
        1;

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    if (!process_cwd_set(
        &instance,
        &tree.home
    )) {
        kernel_panic(
            "Unable to prepare chdir failure fixture"
        );
    }

    /*
     * The target cannot be retained, so chdir must fail atomically and leave
     * the current directory at /home.
     */
    tree.etc.reference_count =
        SIZE_MAX;

    if (
        process_chdir(
            &instance,
            &tree.root,
            "/etc",
            sizeof("/etc") - 1
        ) != PROCESS_PATH_RESULT_RESOURCE_EXHAUSTED ||
        process_cwd_get(
            &instance
        ) != &tree.home ||
        tree.home.reference_count != 2 ||
        tree.etc.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "Failed chdir changed CWD during reference exhaustion"
        );
    }

    tree.etc.reference_count =
        1;

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    if (
        process_chdir(
            NULL,
            &tree.root,
            "/",
            sizeof("/") - 1
        ) != PROCESS_PATH_RESULT_INVALID_ARGUMENT ||
        process_chdir(
            &instance,
            NULL,
            "/",
            sizeof("/") - 1
        ) != PROCESS_PATH_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "chdir accepted invalid process context"
        );
    }

    if (!process_cwd_release(
        &instance
    )) {
        kernel_panic(
            "Unable to release process pathname error CWD"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
    );

    process_path_test_tree_release(
        &tree
    );
}

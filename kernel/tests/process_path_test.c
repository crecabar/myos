// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process_path_test.c
 * @brief Process-relative pathname and chdir regression tests.
 */

#include "process_path_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/cwd.h"
#include "../process/namespace.h"
#include "../process/path.h"
#include "../vfs/mount.h"
#include "../vfs/path.h"
#include "../vfs/vfs.h"

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

static enum vfs_lookup_result process_path_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result
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

static void process_path_test_mount_topology(void);

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
    process_path_test_mount_topology();
    process_path_test_chdir();
    process_path_test_errors();

    diagnostics_write(
        "[process] Pathname context and chdir tests passed\n"
    );
}

static enum vfs_lookup_result process_path_test_lookup(
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

    for (
        size_t index = 0;
        index < fixture->entry_count;
        ++index
    ) {
        const struct process_path_test_entry *entry =
            &fixture->entries[index];

        if (process_path_test_name_equal(
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
    const struct vfs_node *namespace_root =
        process_namespace_root_get(
            instance
        );

    const struct vfs_node *cwd =
        process_cwd_get(
            instance
        );

    if (
        tree->root.reference_count !=
            1U +
            (namespace_root == &tree->root ? 1U : 0U) +
            (cwd == &tree->root ? 1U : 0U) ||
        tree->home.reference_count !=
            (cwd == &tree->home ? 2U : 1U) ||
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
     * No pathname can be resolved before the process owns a namespace root.
     */
    process_path_test_expect_failure(
        &tree,
        &instance,
        "/etc",
        sizeof("/etc") - 1,
        PROCESS_PATH_RESULT_NO_NAMESPACE_ROOT
    );

    if (!process_namespace_root_set(
        &instance,
        &tree.root,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize process pathname namespace root"
        );
    }

    process_path_test_expect_steady_refs(
        &tree,
        &instance
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

    if (!process_namespace_root_release(
        &instance
    )) {
        kernel_panic(
            "Unable to release process pathname namespace root"
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

static void process_path_test_mount_topology(void)
{
    struct vfs_node root;
    struct vfs_node mountpoint;
    struct vfs_node mounted_root;
    struct vfs_node null_device;

    struct process_path_test_entry root_entries[] = {
        {
            .name = "dev",
            .name_length = sizeof("dev") - 1U,
            .node = &mountpoint,
        },
    };

    struct process_path_test_entry mounted_entries[] = {
        {
            .name = "null",
            .name_length = sizeof("null") - 1U,
            .node = &null_device,
        },
    };

    struct process_path_test_directory root_directory = {
        .parent = NULL,
        .entries = root_entries,
        .entry_count =
            sizeof(root_entries) /
            sizeof(root_entries[0]),
    };

    struct process_path_test_directory mountpoint_directory = {
        .parent = &root,
        .entries = NULL,
        .entry_count = 0,
    };

    struct process_path_test_directory mounted_directory = {
        .parent = NULL,
        .entries = mounted_entries,
        .entry_count =
            sizeof(mounted_entries) /
            sizeof(mounted_entries[0]),
    };

    if (
        !vfs_node_initialize(
            &root,
            VFS_NODE_TYPE_DIRECTORY,
            &process_path_test_directory_operations,
            &root_directory
        ) ||
        !vfs_node_initialize(
            &mountpoint,
            VFS_NODE_TYPE_DIRECTORY,
            &process_path_test_directory_operations,
            &mountpoint_directory
        ) ||
        !vfs_node_initialize(
            &mounted_root,
            VFS_NODE_TYPE_DIRECTORY,
            &process_path_test_directory_operations,
            &mounted_directory
        ) ||
        !vfs_node_initialize(
            &null_device,
            VFS_NODE_TYPE_CHARACTER_DEVICE,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize process mount pathname fixture"
        );
    }

    struct vfs_mount_table mounts;

    if (
        !vfs_mount_table_initialize(
            &mounts
        ) ||
        vfs_mount_table_attach(
            &mounts,
            &mountpoint,
            &mounted_root
        ) !=
            VFS_MOUNT_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to initialize process mount topology"
        );
    }

    struct process_instance instance = {0};

    if (
        !process_namespace_root_set(
            &instance,
            &root,
            &mounts
        ) ||
        process_namespace_mounts_get(
            &instance
        ) != &mounts
    ) {
        kernel_panic(
            "Unable to install process mount namespace"
        );
    }

    struct vfs_node *result =
        NULL;

    if (
        process_path_resolve(
            &instance,
            "/dev/null",
            sizeof("/dev/null") - 1U,
            &result
        ) !=
            PROCESS_PATH_RESULT_RESOLVED ||
        result !=
            &null_device
    ) {
        kernel_panic(
            "Process pathname did not traverse namespace mount"
        );
    }

    if (!vfs_node_release(
        result
    )) {
        kernel_panic(
            "Unable to release mounted process pathname result"
        );
    }

    /*
     * The namespace owns the root, while the mount table independently owns
     * its mountpoint and mounted-root references.
     */
    if (
        root.reference_count != 2 ||
        mountpoint.reference_count != 2 ||
        mounted_root.reference_count != 2 ||
        null_device.reference_count != 1
    ) {
        kernel_panic(
            "Process mount pathname leaked VFS references"
        );
    }

    if (
        !process_namespace_root_release(
            &instance
        ) ||
        process_namespace_mounts_get(
            &instance
        ) != NULL ||
        root.reference_count != 1
    ) {
        kernel_panic(
            "Unable to release process mount namespace"
        );
    }

    if (
        vfs_mount_table_detach(
            &mounts,
            &mountpoint
        ) !=
            VFS_MOUNT_RESULT_SUCCESS ||
        mountpoint.reference_count != 1 ||
        mounted_root.reference_count != 1
    ) {
        kernel_panic(
            "Unable to detach process mount topology fixture"
        );
    }

    if (
        !vfs_node_release(
            &null_device
        ) ||
        !vfs_node_release(
            &mounted_root
        ) ||
        !vfs_node_release(
            &mountpoint
        ) ||
        !vfs_node_release(
            &root
        )
    ) {
        kernel_panic(
            "Process mount pathname fixture cleanup failed"
        );
    }
}

static void process_path_test_chdir(void)
{
    struct process_path_test_tree tree;
    struct process_instance instance = {0};

    process_path_test_tree_initialize(
        &tree
    );

    if (!process_namespace_root_set(
        &instance,
        &tree.root,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize chdir namespace root"
        );
    }

    /*
     * An absolute chdir can establish the first CWD of a fresh process.
     */
    if (
        process_chdir(
            &instance,
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

    if (!process_namespace_root_release(
        &instance
    )) {
        kernel_panic(
            "Unable to release chdir test namespace root"
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

    result =
        &tree.sentinel;

    if (
        process_path_resolve(
            &instance,
            "/",
            sizeof("/") - 1,
            &result
        ) != PROCESS_PATH_RESULT_NO_NAMESPACE_ROOT ||
        result != &tree.sentinel ||
        process_path_resolve(
            &instance,
            "etc",
            sizeof("etc") - 1,
            &result
        ) != PROCESS_PATH_RESULT_NO_NAMESPACE_ROOT ||
        result != &tree.sentinel
    ) {
        kernel_panic(
            "Process pathname accepted missing namespace root"
        );
    }

    if (
        process_chdir(
            NULL,
            "/",
            sizeof("/") - 1
        ) != PROCESS_PATH_RESULT_INVALID_ARGUMENT ||
        process_chdir(
            &instance,
            "/",
            sizeof("/") - 1
        ) != PROCESS_PATH_RESULT_NO_NAMESPACE_ROOT
    ) {
        kernel_panic(
            "chdir accepted invalid process namespace context"
        );
    }

    if (!process_namespace_root_set(
        &instance,
        &tree.root,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize pathname error namespace root"
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
        2;

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
            "/",
            sizeof("/") - 1
        ) != PROCESS_PATH_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "chdir accepted invalid process context"
        );
    }

    if (!process_namespace_root_release(
        &instance
    )) {
        kernel_panic(
            "Unable to release pathname error namespace root"
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

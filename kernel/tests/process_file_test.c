// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_file_test.c
 * @brief Process descriptor-level file operation regression tests.
 */

#include "process_file_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/fd_table.h"
#include "../process/file.h"
#include "../process/namespace.h"
#include "../vfs/vfs.h"

#include <stddef.h>

struct process_file_test_directory {
    struct vfs_node *file;
};

struct process_file_test_fixture {
    struct process_instance instance;

    struct vfs_node root;
    struct vfs_node file_node;
    struct vfs_file file_storage;

    struct process_file_test_directory root_directory;
};

static struct vfs_file *process_file_test_open_storage;
static size_t process_file_test_open_call_count;
static enum vfs_open_result process_file_test_open_result;

static struct vfs_node *process_file_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length
);

static enum vfs_open_result process_file_test_open_callback(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result
);

static void process_file_test_fixture_initialize(
    struct process_file_test_fixture *fixture
);

static void process_file_test_fixture_release(
    struct process_file_test_fixture *fixture
);

static void process_file_test_expect_baseline(
    const struct process_file_test_fixture *fixture
);

static void process_file_test_open_close(void);

static void process_file_test_failures(void);

static void process_file_test_full_table_rollback(void);

static const struct vfs_node_operations
    process_file_test_root_operations = {
        .lookup =
            process_file_test_lookup,
    };

static const struct vfs_node_operations
    process_file_test_file_operations = {
        .open =
            process_file_test_open_callback,
    };

void process_file_test_run(void)
{
    process_file_test_open_close();
    process_file_test_failures();
    process_file_test_full_table_rollback();

    diagnostics_write(
        "[process] Descriptor file open/close tests passed\n"
    );
}

static struct vfs_node *process_file_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length)
{
    if (
        directory == NULL ||
        name == NULL
    ) {
        kernel_panic(
            "Process file lookup received invalid input"
        );
    }

    struct process_file_test_directory *fixture =
        directory->private_data;

    if (fixture == NULL) {
        kernel_panic(
            "Process file lookup has no directory fixture"
        );
    }

    if (
        name_length == 4 &&
        name[0] == 'f' &&
        name[1] == 'i' &&
        name[2] == 'l' &&
        name[3] == 'e'
    ) {
        return
            fixture->file;
    }

    return NULL;
}

static enum vfs_open_result process_file_test_open_callback(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result)
{
    ++process_file_test_open_call_count;

    if (
        node == NULL ||
        result == NULL
    ) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (
        process_file_test_open_result !=
        VFS_OPEN_RESULT_OPENED
    ) {
        return
            process_file_test_open_result;
    }

    if (process_file_test_open_storage == NULL) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (!vfs_file_initialize(
        process_file_test_open_storage,
        node,
        access,
        NULL,
        NULL
    )) {
        return
            VFS_OPEN_RESULT_RESOURCE_EXHAUSTED;
    }

    *result =
        process_file_test_open_storage;

    return
        VFS_OPEN_RESULT_OPENED;
}

static void process_file_test_fixture_initialize(
    struct process_file_test_fixture *fixture)
{
    if (fixture == NULL) {
        kernel_panic(
            "Unable to initialize NULL process file fixture"
        );
    }

    *fixture =
        (struct process_file_test_fixture) {0};

    fixture->root_directory.file =
        &fixture->file_node;

    if (
        !process_fd_table_initialize(
            &fixture->instance.file_descriptors
        ) ||
        !vfs_node_initialize(
            &fixture->root,
            VFS_NODE_TYPE_DIRECTORY,
            &process_file_test_root_operations,
            &fixture->root_directory
        ) ||
        !vfs_node_initialize(
            &fixture->file_node,
            VFS_NODE_TYPE_REGULAR_FILE,
            &process_file_test_file_operations,
            NULL
        ) ||
        !process_namespace_root_set(
            &fixture->instance,
            &fixture->root
        )
    ) {
        kernel_panic(
            "Unable to initialize process file fixture"
        );
    }

    process_file_test_open_storage =
        &fixture->file_storage;

    process_file_test_open_call_count =
        0;

    process_file_test_open_result =
        VFS_OPEN_RESULT_OPENED;
}

static void process_file_test_fixture_release(
    struct process_file_test_fixture *fixture)
{
    if (
        !process_fd_table_release_all(
            &fixture->instance.file_descriptors
        ) ||
        !process_namespace_root_release(
            &fixture->instance
        ) ||
        !vfs_node_release(
            &fixture->file_node
        ) ||
        !vfs_node_release(
            &fixture->root
        )
    ) {
        kernel_panic(
            "Process file fixture cleanup failed"
        );
    }
}

static void process_file_test_expect_baseline(
    const struct process_file_test_fixture *fixture)
{
    if (
        fixture->root.reference_count != 2 ||
        fixture->file_node.reference_count != 1
    ) {
        kernel_panic(
            "Process file operation leaked VFS ownership"
        );
    }
}

static void process_file_test_open_close(void)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(
        &fixture
    );

    size_t descriptor =
        777;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0 ||
        process_file_test_open_call_count != 1 ||
        process_fd_table_get(
            &fixture.instance.file_descriptors,
            descriptor
        ) != &fixture.file_storage ||
        fixture.file_storage.reference_count != 1 ||
        fixture.file_storage.node !=
            &fixture.file_node ||
        fixture.file_storage.offset != 0 ||
        fixture.file_storage.access !=
            VFS_OPEN_ACCESS_READ ||
        fixture.file_node.reference_count != 2 ||
        fixture.root.reference_count != 2
    ) {
        kernel_panic(
            "Process file open ownership contract failed"
        );
    }

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        process_fd_table_get(
            &fixture.instance.file_descriptors,
            descriptor
        ) != NULL ||
        fixture.file_storage.reference_count != 0 ||
        fixture.file_node.reference_count != 1
    ) {
        kernel_panic(
            "Process file close ownership contract failed"
        );
    }

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_BAD_DESCRIPTOR
    ) {
        kernel_panic(
            "Process file close accepted empty descriptor"
        );
    }

    process_file_test_expect_baseline(
        &fixture
    );

    process_file_test_fixture_release(
        &fixture
    );
}

static void process_file_test_failures(void)
{
    struct process_instance empty_instance = {0};

    if (!process_fd_table_initialize(
        &empty_instance.file_descriptors
    )) {
        kernel_panic(
            "Unable to initialize empty process file instance"
        );
    }

    size_t descriptor =
        1234;

    if (
        process_file_open(
            &empty_instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_NO_NAMESPACE_ROOT ||
        descriptor != 1234
    ) {
        kernel_panic(
            "Process file open accepted missing namespace root"
        );
    }

    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(
        &fixture
    );

    descriptor =
        1234;

    if (
        process_file_open(
            NULL,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        descriptor != 1234 ||
        process_file_open(
            &fixture.instance,
            NULL,
            1,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        descriptor != 1234 ||
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ,
            NULL
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Process file open accepted invalid arguments"
        );
    }

    if (
        process_file_open(
            &fixture.instance,
            "file",
            sizeof("file") - 1,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_NO_CURRENT_DIRECTORY ||
        descriptor != 1234
    ) {
        kernel_panic(
            "Process file open accepted relative path without CWD"
        );
    }

    if (
        process_file_open(
            &fixture.instance,
            "/missing",
            sizeof("/missing") - 1,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_NOT_FOUND ||
        descriptor != 1234
    ) {
        kernel_panic(
            "Process file open mishandled missing pathname"
        );
    }

    if (
        process_file_open(
            &fixture.instance,
            "/file/child",
            sizeof("/file/child") - 1,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_NOT_DIRECTORY ||
        descriptor != 1234
    ) {
        kernel_panic(
            "Process file open mishandled non-directory traversal"
        );
    }

    process_file_test_open_result =
        VFS_OPEN_RESULT_ACCESS_DENIED;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_ACCESS_DENIED ||
        descriptor != 1234 ||
        fixture.file_node.reference_count != 1
    ) {
        kernel_panic(
            "Process file open mishandled access denial"
        );
    }

    process_file_test_open_result =
        VFS_OPEN_RESULT_OPENED;

    fixture.file_node.operations =
        NULL;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        descriptor != 1234 ||
        fixture.file_node.reference_count != 1
    ) {
        kernel_panic(
            "Process file open accepted missing VFS open operation"
        );
    }

    fixture.file_node.operations =
        &process_file_test_file_operations;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            (enum vfs_open_access) 0,
            &descriptor
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        descriptor != 1234 ||
        fixture.file_node.reference_count != 1
    ) {
        kernel_panic(
            "Process file open accepted invalid access mode"
        );
    }

    if (
        process_file_close(
            NULL,
            0
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        process_file_close(
            &fixture.instance,
            0
        ) != PROCESS_FILE_RESULT_BAD_DESCRIPTOR ||
        process_file_close(
            &fixture.instance,
            PROCESS_FD_TABLE_CAPACITY
        ) != PROCESS_FILE_RESULT_BAD_DESCRIPTOR
    ) {
        kernel_panic(
            "Process file close accepted invalid descriptor context"
        );
    }

    process_file_test_expect_baseline(
        &fixture
    );

    process_file_test_fixture_release(
        &fixture
    );
}

static void process_file_test_full_table_rollback(void)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(
        &fixture
    );

    struct vfs_node filler_node;
    struct vfs_file filler_file;

    if (
        !vfs_node_initialize(
            &filler_node,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &filler_file,
            &filler_node,
            VFS_OPEN_ACCESS_READ,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize full descriptor table fixture"
        );
    }

    for (
        size_t index = 0;
        index < PROCESS_FD_TABLE_CAPACITY;
        ++index
    ) {
        size_t descriptor;

        if (
            !process_fd_table_install(
                &fixture.instance.file_descriptors,
                &filler_file,
                &descriptor
            ) ||
            descriptor != index
        ) {
            kernel_panic(
                "Unable to fill process descriptor table"
            );
        }
    }

    if (
        filler_file.reference_count !=
        1 + PROCESS_FD_TABLE_CAPACITY
    ) {
        kernel_panic(
            "Full process descriptor table has incorrect ownership"
        );
    }

    size_t rejected_descriptor =
        1234;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ,
            &rejected_descriptor
        ) != PROCESS_FILE_RESULT_RESOURCE_EXHAUSTED ||
        rejected_descriptor != 1234 ||
        fixture.file_storage.reference_count != 0 ||
        fixture.file_node.reference_count != 1 ||
        fixture.root.reference_count != 2
    ) {
        kernel_panic(
            "Process file open failed descriptor-table rollback"
        );
    }

    if (
        !process_fd_table_release_all(
            &fixture.instance.file_descriptors
        ) ||
        filler_file.reference_count != 1 ||
        !vfs_file_release(
            &filler_file
        ) ||
        !vfs_node_release(
            &filler_node
        )
    ) {
        kernel_panic(
            "Full descriptor table fixture cleanup failed"
        );
    }

    process_file_test_expect_baseline(
        &fixture
    );

    process_file_test_fixture_release(
        &fixture
    );
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "process_fd_table_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/fd_table.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

static size_t process_fd_table_test_file_destroy_count;

static void process_fd_table_test_file_destroy(
    struct vfs_file *file
);

static void process_fd_table_test_allocation_and_reuse(void);

static void process_fd_table_test_install_at(void);

static void process_fd_table_test_clone(void);

static void process_fd_table_test_capacity(void);

static void process_fd_table_test_clone_rollback(void);

void process_fd_table_test_run(void)
{
    process_fd_table_test_allocation_and_reuse();
    process_fd_table_test_install_at();
    process_fd_table_test_clone();
    process_fd_table_test_clone_rollback();
    process_fd_table_test_capacity();

    diagnostics_write(
        "[process] File descriptor table tests passed\n"
    );
}

static void process_fd_table_test_file_destroy(
    struct vfs_file *file)
{
    if (file == NULL) {
        kernel_panic(
            "FD table file destroy received NULL"
        );
    }

    ++process_fd_table_test_file_destroy_count;
}

static void process_fd_table_test_allocation_and_reuse(void)
{
    struct vfs_node node;
    struct vfs_file file;
    struct process_fd_table table;

    struct vfs_file_operations operations = {
        .destroy =
            process_fd_table_test_file_destroy,
    };

    process_fd_table_test_file_destroy_count =
        0;

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
            &operations,
            NULL
        ) ||
        !process_fd_table_initialize(
            &table
        )
    ) {
        kernel_panic(
            "Unable to initialize FD allocation fixture"
        );
    }

    size_t first_descriptor;
    size_t second_descriptor;

    if (
        !process_fd_table_install(
            &table,
            &file,
            &first_descriptor
        ) ||
        first_descriptor != 0 ||
        !process_fd_table_install(
            &table,
            &file,
            &second_descriptor
        ) ||
        second_descriptor != 1
    ) {
        kernel_panic(
            "FD table did not allocate lowest descriptors"
        );
    }

    if (
        file.reference_count != 3 ||
        process_fd_table_get(
            &table,
            0
        ) != &file ||
        process_fd_table_get(
            &table,
            1
        ) != &file
    ) {
        kernel_panic(
            "FD table ownership after install is incorrect"
        );
    }

    if (!process_fd_table_close(
        &table,
        0
    )) {
        kernel_panic(
            "Unable to close first descriptor"
        );
    }

    if (
        file.reference_count != 2 ||
        process_fd_table_get(
            &table,
            0
        ) != NULL
    ) {
        kernel_panic(
            "FD table close did not release ownership"
        );
    }

    size_t reused_descriptor;

    if (
        !process_fd_table_install(
            &table,
            &file,
            &reused_descriptor
        ) ||
        reused_descriptor != 0
    ) {
        kernel_panic(
            "FD table did not reuse lowest free descriptor"
        );
    }

    if (
        process_fd_table_close(
            &table,
            PROCESS_FD_TABLE_CAPACITY
        ) ||
        process_fd_table_close(
            &table,
            2
        )
    ) {
        kernel_panic(
            "FD table accepted invalid close"
        );
    }

    if (!process_fd_table_release_all(
        &table
    )) {
        kernel_panic(
            "Unable to release FD allocation fixture"
        );
    }

    if (
        file.reference_count != 1 ||
        process_fd_table_test_file_destroy_count != 0
    ) {
        kernel_panic(
            "FD table release-all ownership mismatch"
        );
    }

    if (
        !vfs_file_release(&file) ||
        process_fd_table_test_file_destroy_count != 1 ||
        !vfs_node_release(&node)
    ) {
        kernel_panic(
            "FD allocation fixture cleanup failed"
        );
    }
}

static void process_fd_table_test_install_at(void)
{
    struct vfs_node node;
    struct vfs_file file;

    struct process_fd_table source;
    struct process_fd_table destination;

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
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            NULL,
            NULL
        ) ||
        !process_fd_table_initialize(
            &source
        ) ||
        !process_fd_table_initialize(
            &destination
        )
    ) {
        kernel_panic(
            "Unable to initialize explicit FD installation fixture"
        );
    }

    /*
     * Invalid input must leave the table and file ownership unchanged.
     */
    if (
        process_fd_table_install_at(
            NULL,
            0,
            &file
        ) ||
        process_fd_table_install_at(
            &source,
            0,
            NULL
        ) ||
        process_fd_table_install_at(
            &source,
            PROCESS_FD_TABLE_CAPACITY,
            &file
        ) ||
        file.reference_count != 1
    ) {
        kernel_panic(
            "Explicit FD installation accepted invalid input"
        );
    }

    /*
     * Install the three conventional descriptors without depending on
     * lowest-available descriptor allocation.
     */
    if (
        !process_fd_table_install_at(
            &source,
            2,
            &file
        ) ||
        !process_fd_table_install_at(
            &source,
            0,
            &file
        ) ||
        !process_fd_table_install_at(
            &source,
            1,
            &file
        )
    ) {
        kernel_panic(
            "Unable to install standard descriptor positions"
        );
    }

    if (
        process_fd_table_get(
            &source,
            0
        ) != &file ||
        process_fd_table_get(
            &source,
            1
        ) != &file ||
        process_fd_table_get(
            &source,
            2
        ) != &file ||
        file.reference_count != 4
    ) {
        kernel_panic(
            "Explicit FD installation ownership mismatch"
        );
    }

    /*
     * An occupied slot must not be overwritten or acquire another
     * reference.
     */
    if (
        process_fd_table_install_at(
            &source,
            1,
            &file
        ) ||
        process_fd_table_get(
            &source,
            1
        ) != &file ||
        file.reference_count != 4
    ) {
        kernel_panic(
            "Explicit FD installation replaced occupied descriptor"
        );
    }

    /*
     * Automatic allocation must still select the lowest free slot.
     */
    size_t descriptor =
        SIZE_MAX;

    if (
        !process_fd_table_install(
            &source,
            &file,
            &descriptor
        ) ||
        descriptor != 3 ||
        file.reference_count != 5
    ) {
        kernel_panic(
            "Automatic FD allocation failed after explicit installation"
        );
    }

    /*
     * Clone preserves descriptor positions and shares open-file
     * descriptions through independent ownership references.
     */
    if (!process_fd_table_clone(
        &destination,
        &source
    )) {
        kernel_panic(
            "Unable to clone explicit descriptor table"
        );
    }

    for (size_t index = 0; index < 4; ++index) {
        if (
            process_fd_table_get(
                &destination,
                index
            ) != &file
        ) {
            kernel_panic(
                "FD clone did not preserve descriptor positions"
            );
        }
    }

    if (file.reference_count != 9) {
        kernel_panic(
            "FD clone reference ownership mismatch"
        );
    }

    /*
     * Releasing one table must not invalidate descriptors owned by the
     * other table.
     */
    if (
        !process_fd_table_release_all(
            &source
        ) ||
        file.reference_count != 5 ||
        process_fd_table_get(
            &destination,
            1
        ) != &file
    ) {
        kernel_panic(
            "Source FD release invalidated cloned descriptors"
        );
    }

    if (
        !process_fd_table_release_all(
            &destination
        ) ||
        file.reference_count != 1
    ) {
        kernel_panic(
            "Explicit descriptor fixture leaked file references"
        );
    }

    if (
        !vfs_file_release(
            &file
        ) ||
        !vfs_node_release(
            &node
        )
    ) {
        kernel_panic(
            "Explicit descriptor fixture cleanup failed"
        );
    }
}

static void process_fd_table_test_clone(void)
{
    struct vfs_node node;
    struct vfs_file file;

    struct process_fd_table source;
    struct process_fd_table destination;

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
        ) ||
        !process_fd_table_initialize(
            &source
        ) ||
        !process_fd_table_initialize(
            &destination
        )
    ) {
        kernel_panic(
            "Unable to initialize FD clone fixture"
        );
    }

    size_t descriptor;

    if (!process_fd_table_install(
        &source,
        &file,
        &descriptor
    )) {
        kernel_panic(
            "Unable to install FD clone fixture"
        );
    }

    if (!process_fd_table_clone(
        &destination,
        &source
    )) {
        kernel_panic(
            "Unable to clone FD table"
        );
    }

    if (
        descriptor != 0 ||
        process_fd_table_get(
            &source,
            descriptor
        ) != &file ||
        process_fd_table_get(
            &destination,
            descriptor
        ) != &file ||
        file.reference_count != 3
    ) {
        kernel_panic(
            "FD clone did not preserve shared open file"
        );
    }

    file.offset =
        123;

    if (
        process_fd_table_get(
            &destination,
            descriptor
        )->offset != 123
    ) {
        kernel_panic(
            "FD clone did not share open-file state"
        );
    }

    if (
        !process_fd_table_release_all(
            &source
        ) ||
        file.reference_count != 2 ||
        !process_fd_table_release_all(
            &destination
        ) ||
        file.reference_count != 1 ||
        !vfs_file_release(
            &file
        ) ||
        !vfs_node_release(
            &node
        )
    ) {
        kernel_panic(
            "FD clone fixture cleanup failed"
        );
    }
}

static void process_fd_table_test_capacity(void)
{
    struct vfs_node node;
    struct vfs_file file;
    struct process_fd_table table;

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
        ) ||
        !process_fd_table_initialize(
            &table
        )
    ) {
        kernel_panic(
            "Unable to initialize FD capacity fixture"
        );
    }

    for (size_t expected = 0;
         expected < PROCESS_FD_TABLE_CAPACITY;
         ++expected) {
        size_t descriptor =
            PROCESS_FD_TABLE_CAPACITY;

        if (
            !process_fd_table_install(
                &table,
                &file,
                &descriptor
            ) ||
            descriptor != expected
        ) {
            kernel_panic(
                "FD table capacity allocation failed"
            );
        }
    }

    size_t rejected_descriptor =
        1234;

    if (process_fd_table_install(
        &table,
        &file,
        &rejected_descriptor
    )) {
        kernel_panic(
            "FD table accepted install when full"
        );
    }

    if (rejected_descriptor != 1234) {
        kernel_panic(
            "Failed FD install modified descriptor output"
        );
    }

    if (
        file.reference_count !=
        1 + PROCESS_FD_TABLE_CAPACITY
    ) {
        kernel_panic(
            "Full FD table has incorrect ownership count"
        );
    }

    if (
        !process_fd_table_release_all(
            &table
        ) ||
        file.reference_count != 1 ||
        !vfs_file_release(
            &file
        ) ||
        !vfs_node_release(
            &node
        )
    ) {
        kernel_panic(
            "FD capacity fixture cleanup failed"
        );
    }
}

static void process_fd_table_test_clone_rollback(void)
{
    struct vfs_node first_node;
    struct vfs_node second_node;

    struct vfs_file first_file;
    struct vfs_file second_file;

    struct process_fd_table source;
    struct process_fd_table destination;

    if (
        !vfs_node_initialize(
            &first_node,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &second_node,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &first_file,
            &first_node,
            VFS_OPEN_ACCESS_READ,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &second_file,
            &second_node,
            VFS_OPEN_ACCESS_READ,
            NULL,
            NULL
        ) ||
        !process_fd_table_initialize(
            &source
        ) ||
        !process_fd_table_initialize(
            &destination
        )
    ) {
        kernel_panic(
            "Unable to initialize FD clone rollback fixture"
        );
    }

    size_t first_descriptor;
    size_t second_descriptor;

    if (
        !process_fd_table_install(
            &source,
            &first_file,
            &first_descriptor
        ) ||
        !process_fd_table_install(
            &source,
            &second_file,
            &second_descriptor
        ) ||
        first_descriptor != 0 ||
        second_descriptor != 1
    ) {
        kernel_panic(
            "Unable to populate FD clone rollback fixture"
        );
    }

    /*
     * Force the second retain operation to fail after the first destination
     * reference has already been acquired.
     */
    second_file.reference_count =
        SIZE_MAX;

    if (process_fd_table_clone(
        &destination,
        &source
    )) {
        kernel_panic(
            "FD table clone accepted reference-count overflow"
        );
    }

    if (
        process_fd_table_get(
            &destination,
            first_descriptor
        ) != NULL ||
        process_fd_table_get(
            &destination,
            second_descriptor
        ) != NULL ||
        first_file.reference_count != 2 ||
        second_file.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "FD table clone rollback left partial ownership"
        );
    }

    /*
     * Restore the deliberately corrupted count so normal fixture teardown can
     * release the source table's reference.
     */
    second_file.reference_count =
        2;

    if (
        !process_fd_table_release_all(
            &source
        ) ||
        first_file.reference_count != 1 ||
        second_file.reference_count != 1 ||
        !vfs_file_release(
            &first_file
        ) ||
        !vfs_file_release(
            &second_file
        ) ||
        !vfs_node_release(
            &first_node
        ) ||
        !vfs_node_release(
            &second_node
        )
    ) {
        kernel_panic(
            "FD clone rollback fixture cleanup failed"
        );
    }
}

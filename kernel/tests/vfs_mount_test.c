// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file vfs_mount_test.c
 * @brief VFS mount topology regression tests.
 */

#include "vfs_mount_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../vfs/mount.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

static void vfs_mount_test_lifecycle(void);

static void vfs_mount_test_conflicts(void);

static void vfs_mount_test_reference_failures(void);

static void vfs_mount_test_capacity(void);

void vfs_mount_test_run(void)
{
    vfs_mount_test_lifecycle();
    vfs_mount_test_conflicts();
    vfs_mount_test_reference_failures();
    vfs_mount_test_capacity();

    diagnostics_write(
        "[vfs] Mount topology tests passed\n"
    );
}

static void vfs_mount_test_lifecycle(void)
{
    struct vfs_mount_table table;

    struct vfs_node mountpoint;
    struct vfs_node mounted_root;

    if (
        !vfs_mount_table_initialize(
            &table
        ) ||
        !vfs_node_initialize(
            &mountpoint,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &mounted_root,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize VFS mount lifecycle fixture"
        );
    }

    if (
        vfs_mount_table_attach(
            &table,
            &mountpoint,
            &mounted_root
        ) != VFS_MOUNT_RESULT_SUCCESS ||
        table.count != 1 ||
        mountpoint.reference_count != 2 ||
        mounted_root.reference_count != 2
    ) {
        kernel_panic(
            "VFS mount table did not acquire mount ownership"
        );
    }

    struct vfs_node *result =
        NULL;

    if (
        vfs_mount_table_lookup_mounted_root(
            &table,
            &mountpoint,
            &result
        ) != VFS_MOUNT_RESULT_SUCCESS ||
        result != &mounted_root ||
        mounted_root.reference_count != 3
    ) {
        kernel_panic(
            "VFS mount downward lookup failed"
        );
    }

    if (!vfs_node_release(
        result
    )) {
        kernel_panic(
            "Unable to release mounted-root lookup result"
        );
    }

    result =
        NULL;

    if (
        vfs_mount_table_lookup_mountpoint(
            &table,
            &mounted_root,
            &result
        ) != VFS_MOUNT_RESULT_SUCCESS ||
        result != &mountpoint ||
        mountpoint.reference_count != 3
    ) {
        kernel_panic(
            "VFS mount reverse lookup failed"
        );
    }

    if (!vfs_node_release(
        result
    )) {
        kernel_panic(
            "Unable to release mountpoint lookup result"
        );
    }

    if (
        mountpoint.reference_count != 2 ||
        mounted_root.reference_count != 2
    ) {
        kernel_panic(
            "VFS mount lookup leaked node ownership"
        );
    }

    if (
        vfs_mount_table_detach(
            &table,
            &mountpoint
        ) != VFS_MOUNT_RESULT_SUCCESS ||
        table.count != 0 ||
        mountpoint.reference_count != 1 ||
        mounted_root.reference_count != 1
    ) {
        kernel_panic(
            "VFS mount detach did not release ownership"
        );
    }

    result =
        &mounted_root;

    if (
        vfs_mount_table_lookup_mounted_root(
            &table,
            &mountpoint,
            &result
        ) != VFS_MOUNT_RESULT_NOT_FOUND ||
        result != &mounted_root
    ) {
        kernel_panic(
            "Detached VFS mount remained discoverable"
        );
    }

    if (
        !vfs_node_release(
            &mounted_root
        ) ||
        !vfs_node_release(
            &mountpoint
        )
    ) {
        kernel_panic(
            "VFS mount lifecycle fixture cleanup failed"
        );
    }
}

static void vfs_mount_test_conflicts(void)
{
    struct vfs_mount_table table;

    struct vfs_node mountpoint;
    struct vfs_node alternate_mountpoint;
    struct vfs_node mounted_root;
    struct vfs_node alternate_root;
    struct vfs_node regular;

    if (
        !vfs_mount_table_initialize(
            &table
        ) ||
        !vfs_node_initialize(
            &mountpoint,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &alternate_mountpoint,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &mounted_root,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &alternate_root,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &regular,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize VFS mount conflict fixture"
        );
    }

    if (
        vfs_mount_table_attach(
            NULL,
            &mountpoint,
            &mounted_root
        ) != VFS_MOUNT_RESULT_INVALID_ARGUMENT ||
        vfs_mount_table_attach(
            &table,
            NULL,
            &mounted_root
        ) != VFS_MOUNT_RESULT_INVALID_ARGUMENT ||
        vfs_mount_table_attach(
            &table,
            &mountpoint,
            NULL
        ) != VFS_MOUNT_RESULT_INVALID_ARGUMENT ||
        vfs_mount_table_attach(
            &table,
            &regular,
            &mounted_root
        ) != VFS_MOUNT_RESULT_INVALID_ARGUMENT ||
        vfs_mount_table_attach(
            &table,
            &mountpoint,
            &regular
        ) != VFS_MOUNT_RESULT_INVALID_ARGUMENT ||
        vfs_mount_table_attach(
            &table,
            &mountpoint,
            &mountpoint
        ) != VFS_MOUNT_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "VFS mount table accepted invalid relation"
        );
    }

    if (
        vfs_mount_table_attach(
            &table,
            &mountpoint,
            &mounted_root
        ) != VFS_MOUNT_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to prepare VFS mount conflict fixture"
        );
    }

    if (
        vfs_mount_table_attach(
            &table,
            &mountpoint,
            &alternate_root
        ) != VFS_MOUNT_RESULT_CONFLICT ||
        vfs_mount_table_attach(
            &table,
            &alternate_mountpoint,
            &mounted_root
        ) != VFS_MOUNT_RESULT_CONFLICT ||
        vfs_mount_table_attach(
            &table,
            &mounted_root,
            &alternate_root
        ) != VFS_MOUNT_RESULT_CONFLICT ||
        vfs_mount_table_attach(
            &table,
            &alternate_mountpoint,
            &mountpoint
        ) != VFS_MOUNT_RESULT_CONFLICT ||
        table.count != 1 ||
        mountpoint.reference_count != 2 ||
        mounted_root.reference_count != 2 ||
        alternate_mountpoint.reference_count != 1 ||
        alternate_root.reference_count != 1
    ) {
        kernel_panic(
            "VFS mount conflict changed installed topology"
        );
    }

    if (
        vfs_mount_table_detach(
            &table,
            &alternate_mountpoint
        ) != VFS_MOUNT_RESULT_NOT_FOUND ||
        vfs_mount_table_detach(
            &table,
            &mountpoint
        ) != VFS_MOUNT_RESULT_SUCCESS
    ) {
        kernel_panic(
            "VFS mount conflict fixture detach failed"
        );
    }

    if (
        !vfs_node_release(
            &regular
        ) ||
        !vfs_node_release(
            &alternate_root
        ) ||
        !vfs_node_release(
            &mounted_root
        ) ||
        !vfs_node_release(
            &alternate_mountpoint
        ) ||
        !vfs_node_release(
            &mountpoint
        )
    ) {
        kernel_panic(
            "VFS mount conflict fixture cleanup failed"
        );
    }
}

static void vfs_mount_test_reference_failures(void)
{
    struct vfs_mount_table table;

    struct vfs_node mountpoint;
    struct vfs_node mounted_root;

    if (
        !vfs_mount_table_initialize(
            &table
        ) ||
        !vfs_node_initialize(
            &mountpoint,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !vfs_node_initialize(
            &mounted_root,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize VFS mount reference fixture"
        );
    }

    mountpoint.reference_count =
        SIZE_MAX;

    if (
        vfs_mount_table_attach(
            &table,
            &mountpoint,
            &mounted_root
        ) != VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED ||
        table.count != 0 ||
        mountpoint.reference_count != SIZE_MAX ||
        mounted_root.reference_count != 1
    ) {
        kernel_panic(
            "VFS mount accepted exhausted mountpoint reference"
        );
    }

    mountpoint.reference_count =
        1;

    mounted_root.reference_count =
        SIZE_MAX;

    if (
        vfs_mount_table_attach(
            &table,
            &mountpoint,
            &mounted_root
        ) != VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED ||
        table.count != 0 ||
        mountpoint.reference_count != 1 ||
        mounted_root.reference_count != SIZE_MAX
    ) {
        kernel_panic(
            "VFS mount attach rollback leaked mountpoint reference"
        );
    }

    mounted_root.reference_count =
        1;

    if (
        vfs_mount_table_attach(
            &table,
            &mountpoint,
            &mounted_root
        ) != VFS_MOUNT_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to prepare VFS mount lookup exhaustion fixture"
        );
    }

    mounted_root.reference_count =
        SIZE_MAX;

    struct vfs_node *result =
        &mountpoint;

    if (
        vfs_mount_table_lookup_mounted_root(
            &table,
            &mountpoint,
            &result
        ) != VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED ||
        result != &mountpoint
    ) {
        kernel_panic(
            "VFS mount lookup violated reference failure contract"
        );
    }

    mounted_root.reference_count =
        2;

    mountpoint.reference_count =
        SIZE_MAX;

    result =
        &mounted_root;

    if (
        vfs_mount_table_lookup_mountpoint(
            &table,
            &mounted_root,
            &result
        ) != VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED ||
        result != &mounted_root
    ) {
        kernel_panic(
            "VFS mount reverse lookup violated failure contract"
        );
    }

    mountpoint.reference_count =
        2;

    if (
        vfs_mount_table_detach(
            &table,
            &mountpoint
        ) != VFS_MOUNT_RESULT_SUCCESS ||
        mountpoint.reference_count != 1 ||
        mounted_root.reference_count != 1
    ) {
        kernel_panic(
            "Unable to detach VFS mount reference fixture"
        );
    }

    if (
        !vfs_node_release(
            &mounted_root
        ) ||
        !vfs_node_release(
            &mountpoint
        )
    ) {
        kernel_panic(
            "VFS mount reference fixture cleanup failed"
        );
    }
}

static void vfs_mount_test_capacity(void)
{
    struct vfs_mount_table table;

    struct vfs_node mountpoints[
        VFS_MOUNT_TABLE_CAPACITY + 1
    ];

    struct vfs_node mounted_roots[
        VFS_MOUNT_TABLE_CAPACITY + 1
    ];

    if (!vfs_mount_table_initialize(
        &table
    )) {
        kernel_panic(
            "Unable to initialize VFS mount capacity table"
        );
    }

    for (size_t index = 0;
         index < VFS_MOUNT_TABLE_CAPACITY + 1;
         ++index) {
        if (
            !vfs_node_initialize(
                &mountpoints[index],
                VFS_NODE_TYPE_DIRECTORY,
                NULL,
                NULL
            ) ||
            !vfs_node_initialize(
                &mounted_roots[index],
                VFS_NODE_TYPE_DIRECTORY,
                NULL,
                NULL
            )
        ) {
            kernel_panic(
                "Unable to initialize VFS mount capacity fixture"
            );
        }
    }

    for (size_t index = 0;
         index < VFS_MOUNT_TABLE_CAPACITY;
         ++index) {
        if (
            vfs_mount_table_attach(
                &table,
                &mountpoints[index],
                &mounted_roots[index]
            ) != VFS_MOUNT_RESULT_SUCCESS
        ) {
            kernel_panic(
                "VFS mount table exhausted before declared capacity"
            );
        }
    }

    if (
        table.count !=
            VFS_MOUNT_TABLE_CAPACITY ||
        vfs_mount_table_attach(
            &table,
            &mountpoints[
                VFS_MOUNT_TABLE_CAPACITY
            ],
            &mounted_roots[
                VFS_MOUNT_TABLE_CAPACITY
            ]
        ) !=
            VFS_MOUNT_RESULT_RESOURCE_EXHAUSTED ||
        mountpoints[
            VFS_MOUNT_TABLE_CAPACITY
        ].reference_count != 1 ||
        mounted_roots[
            VFS_MOUNT_TABLE_CAPACITY
        ].reference_count != 1
    ) {
        kernel_panic(
            "VFS mount table capacity contract failed"
        );
    }

    for (size_t index = 0;
         index < VFS_MOUNT_TABLE_CAPACITY;
         ++index) {
        if (
            vfs_mount_table_detach(
                &table,
                &mountpoints[index]
            ) != VFS_MOUNT_RESULT_SUCCESS
        ) {
            kernel_panic(
                "Unable to empty VFS mount capacity table"
            );
        }
    }

    if (table.count != 0) {
        kernel_panic(
            "VFS mount capacity table retained relations"
        );
    }

    for (size_t index = 0;
         index < VFS_MOUNT_TABLE_CAPACITY + 1;
         ++index) {
        if (
            mountpoints[index].reference_count != 1 ||
            mounted_roots[index].reference_count != 1 ||
            !vfs_node_release(
                &mounted_roots[index]
            ) ||
            !vfs_node_release(
                &mountpoints[index]
            )
        ) {
            kernel_panic(
                "VFS mount capacity fixture leaked ownership"
            );
        }
    }
}

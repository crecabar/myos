// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file devfs_test.c
 * @brief Dynamic device-filesystem regression tests.
 */

#include "devfs_test.h"

#include "../core/device/registry.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../drivers/pseudo.h"
#include "../fs/devfs.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static void devfs_test_unregister_device(
    struct device_registry *registry,
    struct device *device
);

static void devfs_test_dynamic_lookup(void);

// Public functions implementations
void devfs_test_run(void)
{
    devfs_test_dynamic_lookup();

    diagnostics_write(
        "[fs] devfs dynamic lookup and lifetime tests passed\n"
    );
}

// Private functions and helpers implementations
static void devfs_test_unregister_device(
    struct device_registry *registry,
    struct device *device)
{
    struct device *detached =
        NULL;

    if (
        registry == NULL ||
        device == NULL ||
        device_registry_unregister(
            registry,
            device,
            &detached
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
        detached !=
            device ||
        !device_finish_removal(
            detached
        ) ||
        !device_release(
            detached
        ) ||
        !device_release(
            device
        )
    ) {
        kernel_panic(
            "devfs fixture device cleanup failed"
        );
    }
}

static void devfs_test_dynamic_lookup(void)
{
    struct device_registry registry;

    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "devfs registry fixture initialization failed"
        );
    }

    /*
     * Mount devfs before publishing any device. This verifies that devfs is a
     * live registry projection rather than a snapshot captured at mount time.
     */
    struct devfs *filesystem =
        NULL;

    if (
        devfs_mount(
            &registry,
            &filesystem
        ) !=
            DEVFS_MOUNT_RESULT_MOUNTED ||
        filesystem == NULL
    ) {
        kernel_panic(
            "devfs mount failed"
        );
    }

    struct vfs_node *root =
        devfs_root(
            filesystem
        );

    if (
        root == NULL ||
        root->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        root->reference_count != 1
    ) {
        kernel_panic(
            "devfs root contract failed"
        );
    }

    struct vfs_stat root_metadata = {
        .type =
            VFS_NODE_TYPE_REGULAR_FILE,
        .size =
            UINT64_MAX,
    };

    if (
        vfs_node_stat(
            root,
            &root_metadata
        ) !=
            VFS_STAT_RESULT_SUCCESS ||
        root_metadata.type !=
            VFS_NODE_TYPE_DIRECTORY ||
        root_metadata.size != 0
    ) {
        kernel_panic(
            "devfs root stat failed"
        );
    }

    struct vfs_node *missing =
        NULL;

    if (
        vfs_node_lookup(
            root,
            "null",
            sizeof("null") - 1U,
            &missing
        ) !=
            VFS_LOOKUP_RESULT_NOT_FOUND ||
        missing != NULL
    ) {
        kernel_panic(
            "devfs empty-registry lookup failed"
        );
    }

    struct pseudo_devices devices;

    if (
        pseudo_devices_initialize(
            &devices,
            &registry
        ) !=
            PSEUDO_DEVICES_INITIALIZE_RESULT_INITIALIZED
    ) {
        kernel_panic(
            "devfs pseudo-device publication failed"
        );
    }

    if (
        devices.null_device.device.reference_count != 2 ||
        devices.zero_device.device.reference_count != 2
    ) {
        kernel_panic(
            "devfs pseudo-device initial ownership invalid"
        );
    }

    struct vfs_node *null_node =
        NULL;

    if (
        vfs_node_lookup(
            root,
            "null",
            sizeof("null") - 1U,
            &null_node
        ) !=
            VFS_LOOKUP_RESULT_FOUND ||
        null_node == NULL ||
        null_node->type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        null_node->reference_count != 2 ||
        devices.null_device.device.reference_count != 3
    ) {
        kernel_panic(
            "devfs dynamic null lookup failed"
        );
    }

    /*
     * Generic VFS components may be longer than device names. Such a valid
     * pathname component cannot name a devfs entry and is therefore simply
     * absent rather than invalid.
     */
    char oversized_device_name[
        DEVICE_NAME_MAX + 1U
    ];

    for (
        size_t index = 0;
        index < sizeof(oversized_device_name);
        ++index
    ) {
        oversized_device_name[index] =
            'x';
    }

    struct vfs_node *oversized =
        NULL;

    if (
        vfs_node_lookup(
            root,
            oversized_device_name,
            sizeof(oversized_device_name),
            &oversized
        ) !=
            VFS_LOOKUP_RESULT_NOT_FOUND ||
        oversized != NULL
    ) {
        kernel_panic(
            "devfs oversized device-name lookup failed"
        );
    }

    /*
     * A second lookup of the same registered device must reuse the vnode that
     * devfs already materialized rather than constructing another adapter.
     */
    struct vfs_node *same_null_node =
        NULL;

    if (
        vfs_node_lookup(
            root,
            "null",
            sizeof("null") - 1U,
            &same_null_node
        ) !=
            VFS_LOOKUP_RESULT_FOUND ||
        same_null_node !=
            null_node ||
        null_node->reference_count != 3 ||
        devices.null_device.device.reference_count != 3
    ) {
        kernel_panic(
            "devfs vnode cache identity failed"
        );
    }

    /*
     * The published object must behave as an ordinary VFS character node.
     */
    struct vfs_file *file =
        NULL;

    if (
        vfs_node_open(
            null_node,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &file
        ) !=
            VFS_OPEN_RESULT_OPENED ||
        file == NULL ||
        null_node->reference_count != 4
    ) {
        kernel_panic(
            "devfs character-device open failed"
        );
    }

    const uint8_t data[] = {
        0x11U,
        0x22U,
        0x33U,
        0x44U,
    };

    size_t written =
        SIZE_MAX;

    if (
        vfs_file_write(
            file,
            data,
            sizeof(data),
            &written
        ) !=
            VFS_IO_RESULT_SUCCESS ||
        written !=
            sizeof(data)
    ) {
        kernel_panic(
            "devfs character-device write failed"
        );
    }

    /*
     * An outstanding file/node reference must prevent filesystem teardown.
     * Rejected unmount is transactional and therefore leaves everything live.
     */
    if (devfs_unmount(
        filesystem
    )) {
        kernel_panic(
            "devfs unmounted with outstanding vnode references"
        );
    }

    if (
        !vfs_file_release(
            file
        ) ||
        null_node->reference_count != 3
    ) {
        kernel_panic(
            "devfs open-file cleanup failed"
        );
    }

    if (
        !vfs_node_release(
            same_null_node
        ) ||
        !vfs_node_release(
            null_node
        ) ||
        null_node->reference_count != 1
    ) {
        kernel_panic(
            "devfs lookup ownership cleanup failed"
        );
    }

    /*
     * Withdrawal from the registry must immediately remove pathname
     * visibility. The cached vnode continues owning the generic device until
     * devfs itself is unmounted.
     */
    devfs_test_unregister_device(
        &registry,
        &devices.null_device.device
    );

    if (
        devices.null_device.device.state !=
            DEVICE_STATE_GONE ||
        devices.null_device.device.reference_count != 1
    ) {
        kernel_panic(
            "devfs cached-device removal ownership invalid"
        );
    }

    struct vfs_node *withdrawn =
        NULL;

    if (
        vfs_node_lookup(
            root,
            "null",
            sizeof("null") - 1U,
            &withdrawn
        ) !=
            VFS_LOOKUP_RESULT_NOT_FOUND ||
        withdrawn != NULL
    ) {
        kernel_panic(
            "devfs exposed withdrawn device"
        );
    }

    /*
     * zero was never materialized by devfs, so ordinary registry withdrawal
     * can release its complete lifetime immediately.
     */
    devfs_test_unregister_device(
        &registry,
        &devices.zero_device.device
    );

    if (
        devices.zero_device.device.state !=
            DEVICE_STATE_GONE ||
        devices.zero_device.device.reference_count != 0
    ) {
        kernel_panic(
            "devfs unmaterialized device cleanup failed"
        );
    }

    /*
     * Releasing devfs destroys the cached character adapter. That adapter owns
     * the final reference keeping the withdrawn null device physically alive.
     */
    if (
        !devfs_unmount(
            filesystem
        ) ||
        devices.null_device.device.reference_count != 0
    ) {
        kernel_panic(
            "devfs final unmount cleanup failed"
        );
    }
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file vfs_character_device_test.c
 * @brief Character-device VFS adapter regression tests.
 */

#include "vfs_character_device_test.h"

#include "../core/device/registry.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../drivers/pseudo.h"
#include "../vfs/character_device.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static void vfs_character_device_test_unregister(
    struct device_registry *registry,
    struct device *device
);

static void vfs_character_device_test_null(void);

static void vfs_character_device_test_zero(void);

// Public functions implementations
void vfs_character_device_test_run(void)
{
    vfs_character_device_test_null();
    vfs_character_device_test_zero();

    diagnostics_write(
        "[vfs] Character-device adapter tests passed\n"
    );
}

// Private functions and helpers implementations
static void vfs_character_device_test_unregister(
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
            "Character VFS fixture device cleanup failed"
        );
    }
}

static void vfs_character_device_test_null(void)
{
    struct device_registry registry;
    struct pseudo_devices devices;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        pseudo_devices_initialize(
            &devices,
            &registry
        ) !=
            PSEUDO_DEVICES_INITIALIZE_RESULT_INITIALIZED
    ) {
        kernel_panic(
            "Character VFS null fixture setup failed"
        );
    }

    struct vfs_character_device_node node;

    if (
        !vfs_character_device_node_initialize(
            &node,
            &devices.null_device.character
        ) ||
        node.vfs.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        node.vfs.reference_count != 1 ||
        devices.null_device.device.reference_count != 3
    ) {
        kernel_panic(
            "Character VFS null node initialization failed"
        );
    }

    struct vfs_stat metadata = {
        .type =
            VFS_NODE_TYPE_REGULAR_FILE,
        .size =
            UINT64_MAX,
    };

    if (
        vfs_node_stat(
            &node.vfs,
            &metadata
        ) != VFS_STAT_RESULT_SUCCESS ||
        metadata.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        metadata.size != 0
    ) {
        kernel_panic(
            "Character VFS null stat failed"
        );
    }

    struct vfs_file *file =
        NULL;

    if (
        vfs_node_open(
            &node.vfs,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &file
        ) != VFS_OPEN_RESULT_OPENED ||
        file == NULL ||
        node.vfs.reference_count != 2
    ) {
        kernel_panic(
            "Character VFS null open failed"
        );
    }

    uint8_t buffer[4] = {
        0x11U,
        0x22U,
        0x33U,
        0x44U,
    };

    size_t transferred =
        SIZE_MAX;

    if (
        vfs_file_read(
            file,
            buffer,
            sizeof(buffer),
            &transferred
        ) != VFS_IO_RESULT_SUCCESS ||
        transferred != 0 ||
        file->offset != 0 ||
        buffer[0] != 0x11U ||
        buffer[1] != 0x22U ||
        buffer[2] != 0x33U ||
        buffer[3] != 0x44U
    ) {
        kernel_panic(
            "Character VFS null read failed"
        );
    }

    transferred =
        SIZE_MAX;

    if (
        vfs_file_write(
            file,
            buffer,
            sizeof(buffer),
            &transferred
        ) != VFS_IO_RESULT_SUCCESS ||
        transferred !=
            sizeof(buffer) ||
        file->offset !=
            sizeof(buffer)
    ) {
        kernel_panic(
            "Character VFS null write failed"
        );
    }

    uint64_t seek_result =
        UINT64_MAX;

    if (
        vfs_file_seek(
            file,
            0,
            VFS_SEEK_ORIGIN_START,
            &seek_result
        ) !=
            VFS_SEEK_RESULT_NOT_SUPPORTED ||
        seek_result !=
            UINT64_MAX
    ) {
        kernel_panic(
            "Character VFS null unexpectedly supported seek"
        );
    }

    if (
        !vfs_file_release(
            file
        ) ||
        node.vfs.reference_count != 1 ||
        !vfs_node_release(
            &node.vfs
        ) ||
        node.vfs.reference_count != 0 ||
        devices.null_device.device.reference_count != 2
    ) {
        kernel_panic(
            "Character VFS null ownership cleanup failed"
        );
    }

    vfs_character_device_test_unregister(
        &registry,
        &devices.zero_device.device
    );

    vfs_character_device_test_unregister(
        &registry,
        &devices.null_device.device
    );
}

static void vfs_character_device_test_zero(void)
{
    struct device_registry registry;
    struct pseudo_devices devices;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        pseudo_devices_initialize(
            &devices,
            &registry
        ) !=
            PSEUDO_DEVICES_INITIALIZE_RESULT_INITIALIZED
    ) {
        kernel_panic(
            "Character VFS zero fixture setup failed"
        );
    }

    struct vfs_character_device_node node;

    if (!vfs_character_device_node_initialize(
        &node,
        &devices.zero_device.character
    )) {
        kernel_panic(
            "Character VFS zero node initialization failed"
        );
    }

    struct vfs_file *file =
        NULL;

    if (
        vfs_node_open(
            &node.vfs,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &file
        ) != VFS_OPEN_RESULT_OPENED ||
        file == NULL
    ) {
        kernel_panic(
            "Character VFS zero open failed"
        );
    }

    uint8_t buffer[8] = {
        0xa1U,
        0xa2U,
        0xa3U,
        0xa4U,
        0xb1U,
        0xb2U,
        0xb3U,
        0xb4U,
    };

    size_t transferred =
        SIZE_MAX;

    if (
        vfs_file_read(
            file,
            buffer,
            sizeof(buffer),
            &transferred
        ) != VFS_IO_RESULT_SUCCESS ||
        transferred !=
            sizeof(buffer) ||
        file->offset !=
            sizeof(buffer)
    ) {
        kernel_panic(
            "Character VFS zero read failed"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(buffer);
        ++index
    ) {
        if (buffer[index] != 0) {
            kernel_panic(
                "Character VFS zero read returned non-zero data"
            );
        }
    }

    transferred =
        SIZE_MAX;

    if (
        vfs_file_write(
            file,
            buffer,
            sizeof(buffer),
            &transferred
        ) != VFS_IO_RESULT_SUCCESS ||
        transferred !=
            sizeof(buffer) ||
        file->offset !=
            sizeof(buffer) * 2U
    ) {
        kernel_panic(
            "Character VFS zero write failed"
        );
    }

    if (
        !vfs_file_release(
            file
        ) ||
        !vfs_node_release(
            &node.vfs
        )
    ) {
        kernel_panic(
            "Character VFS zero ownership cleanup failed"
        );
    }

    vfs_character_device_test_unregister(
        &registry,
        &devices.zero_device.device
    );

    vfs_character_device_test_unregister(
        &registry,
        &devices.null_device.device
    );
}

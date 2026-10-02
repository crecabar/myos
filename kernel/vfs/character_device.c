// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file character_device.c
 * @brief VFS character-device adapter implementation.
 */

#include "character_device.h"

#include "../core/panic.h"
#include "../memory/heap.h"

#include <stddef.h>
#include <stdint.h>

struct vfs_character_device_file {
    struct vfs_file vfs;

    struct vfs_character_device_node *node;
};

// Private functions and helpers declarations
static struct vfs_character_device_node *
vfs_character_device_node_from_vfs(
    struct vfs_node *node
);

static struct vfs_character_device_file *
vfs_character_device_file_from_vfs(
    struct vfs_file *file
);

static enum vfs_io_result vfs_character_device_map_io_result(
    enum character_device_io_result result
);

static enum vfs_open_result vfs_character_device_open(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result
);

static enum vfs_stat_result vfs_character_device_stat(
    struct vfs_node *node,
    struct vfs_stat *result
);

static void vfs_character_device_node_destroy(
    struct vfs_node *node
);

static enum vfs_io_result vfs_character_device_read(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

static enum vfs_io_result vfs_character_device_write(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

static void vfs_character_device_file_destroy(
    struct vfs_file *file
);

// Static local variables
static const struct vfs_node_operations
    vfs_character_device_node_operations = {
        .open =
            vfs_character_device_open,
        .stat =
            vfs_character_device_stat,
        .lookup =
            NULL,
        .parent =
            NULL,
        .destroy =
            vfs_character_device_node_destroy,
    };

static const struct vfs_file_operations
    vfs_character_device_file_operations = {
        .read =
            vfs_character_device_read,
        .write =
            vfs_character_device_write,
        .destroy =
            vfs_character_device_file_destroy,
    };

// Public functions implementations
bool vfs_character_device_node_initialize(
    struct vfs_character_device_node *node,
    struct character_device *character)
{
    if (
        node == NULL ||
        character == NULL ||
        character->device == NULL
    ) {
        return false;
    }

    /*
     * Capability discovery also verifies that the underlying generic device
     * is currently live and ACTIVE.
     */
    bool readable =
        character_device_can_read(
            character
        );

    bool writable =
        character_device_can_write(
            character
        );

    if (
        !readable &&
        !writable
    ) {
        return false;
    }

    /*
     * VFS publication owns an independent generic-device reference. Acquire
     * it before publishing any VFS state so failure remains transactional.
     */
    if (!device_retain(
        character->device
    )) {
        return false;
    }

    node->character =
        character;

    if (!vfs_node_initialize(
        &node->vfs,
        VFS_NODE_TYPE_CHARACTER_DEVICE,
        &vfs_character_device_node_operations,
        node
    )) {
        if (!device_release(
            character->device
        )) {
            kernel_panic(
                "Character VFS adapter failed to roll back device ownership"
            );
        }

        node->character =
            NULL;

        return false;
    }

    return true;
}

// Private functions and helpers implementations
static struct vfs_character_device_node *
vfs_character_device_node_from_vfs(
    struct vfs_node *node)
{
    if (
        node == NULL ||
        node->reference_count == 0 ||
        node->type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        node->private_data == NULL
    ) {
        return NULL;
    }

    struct vfs_character_device_node *adapter =
        node->private_data;

    if (
        &adapter->vfs !=
            node ||
        adapter->character == NULL ||
        adapter->character->device == NULL
    ) {
        return NULL;
    }

    return
        adapter;
}

static struct vfs_character_device_file *
vfs_character_device_file_from_vfs(
    struct vfs_file *file)
{
    if (
        file == NULL ||
        file->reference_count == 0 ||
        file->node == NULL ||
        file->private_data == NULL
    ) {
        return NULL;
    }

    struct vfs_character_device_file *open_file =
        file->private_data;

    if (
        &open_file->vfs !=
            file ||
        open_file->node == NULL ||
        file->node !=
            &open_file->node->vfs
    ) {
        return NULL;
    }

    return
        open_file;
}

static enum vfs_io_result vfs_character_device_map_io_result(
    enum character_device_io_result result)
{
    switch (result) {
        case CHARACTER_DEVICE_IO_RESULT_SUCCESS:
            return
                VFS_IO_RESULT_SUCCESS;

        case CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT:
            return
                VFS_IO_RESULT_INVALID_ARGUMENT;

        case CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED:
            return
                VFS_IO_RESULT_NOT_SUPPORTED;

        case CHARACTER_DEVICE_IO_RESULT_RESOURCE_EXHAUSTED:
            return
                VFS_IO_RESULT_RESOURCE_EXHAUSTED;
    }

    return
        VFS_IO_RESULT_INVALID_ARGUMENT;
}

static enum vfs_open_result vfs_character_device_open(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result)
{
    if (
        result == NULL
    ) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_character_device_node *adapter =
        vfs_character_device_node_from_vfs(
            node
        );

    if (adapter == NULL) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (
        (
            (access &
                VFS_OPEN_ACCESS_READ) != 0 &&
            !character_device_can_read(
                adapter->character
            )
        ) ||
        (
            (access &
                VFS_OPEN_ACCESS_WRITE) != 0 &&
            !character_device_can_write(
                adapter->character
            )
        )
    ) {
        return
            VFS_OPEN_RESULT_NOT_SUPPORTED;
    }

    struct vfs_character_device_file *open_file =
        kmalloc(sizeof(*open_file));

    if (open_file == NULL) {
        return
            VFS_OPEN_RESULT_RESOURCE_EXHAUSTED;
    }

    open_file->node =
        adapter;

    if (!vfs_file_initialize(
        &open_file->vfs,
        node,
        access,
        &vfs_character_device_file_operations,
        open_file
    )) {
        kfree(
            open_file
        );

        return
            VFS_OPEN_RESULT_RESOURCE_EXHAUSTED;
    }

    *result =
        &open_file->vfs;

    return
        VFS_OPEN_RESULT_OPENED;
}

static enum vfs_stat_result vfs_character_device_stat(
    struct vfs_node *node,
    struct vfs_stat *result)
{
    if (
        result == NULL ||
        vfs_character_device_node_from_vfs(
            node
        ) == NULL
    ) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    result->type =
        VFS_NODE_TYPE_CHARACTER_DEVICE;

    /*
     * Character devices do not expose a byte-addressable file length.
     */
    result->size =
        0;

    return
        VFS_STAT_RESULT_SUCCESS;
}

static void vfs_character_device_node_destroy(
    struct vfs_node *node)
{
    struct vfs_character_device_node *adapter =
        node != NULL
            ? node->private_data
            : NULL;

    if (
        adapter == NULL ||
        &adapter->vfs !=
            node ||
        adapter->character == NULL ||
        adapter->character->device == NULL
    ) {
        kernel_panic(
            "Character VFS node ownership corrupted during destroy"
        );
    }

    struct device *device =
        adapter->character->device;

    /*
     * The adapter storage is caller-owned. Only the independent device
     * ownership acquired at initialization belongs to this destroy callback.
     */
    adapter->character =
        NULL;

    if (!device_release(
        device
    )) {
        kernel_panic(
            "Character VFS node failed to release device ownership"
        );
    }
}

static enum vfs_io_result vfs_character_device_read(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    (void) offset;

    struct vfs_character_device_file *open_file =
        vfs_character_device_file_from_vfs(
            file
        );

    if (
        open_file == NULL ||
        bytes_read == NULL
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    return
        vfs_character_device_map_io_result(
            character_device_read(
                open_file->node->character,
                buffer,
                size,
                bytes_read
            )
        );
}

static enum vfs_io_result vfs_character_device_write(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    (void) offset;

    struct vfs_character_device_file *open_file =
        vfs_character_device_file_from_vfs(
            file
        );

    if (
        open_file == NULL ||
        bytes_written == NULL
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    return
        vfs_character_device_map_io_result(
            character_device_write(
                open_file->node->character,
                buffer,
                size,
                bytes_written
            )
        );
}

static void vfs_character_device_file_destroy(
    struct vfs_file *file)
{
    if (file == NULL) {
        kernel_panic(
            "Character VFS file destroy received null file"
        );
    }

    struct vfs_character_device_file *open_file =
        file->private_data;

    if (
        open_file == NULL ||
        &open_file->vfs !=
            file
    ) {
        kernel_panic(
            "Character VFS open-file ownership corrupted"
        );
    }

    /*
     * vfs_file_release() releases the node reference after this callback.
     */
    kfree(
        open_file
    );
}

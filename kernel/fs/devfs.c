// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file devfs.c
 * @brief Dynamic device-filesystem implementation.
 */

#include "devfs.h"

#include "../core/device/character_device.h"
#include "../core/panic.h"
#include "../memory/heap.h"
#include "../vfs/character_device.h"

#include <stddef.h>

struct devfs_entry {
    struct vfs_character_device_node character;
};

struct devfs_directory_file {
    struct vfs_file vfs;

    struct device_registry_iterator iterator;
};

struct devfs {
    const struct device_registry *registry;

    struct vfs_node root;

    struct devfs_entry *entries[
        DEVICE_REGISTRY_CAPACITY
    ];

    size_t entry_count;
};

// Private functions and helpers declarations
static struct devfs *devfs_from_root(
    struct vfs_node *node
);

static struct devfs_entry *devfs_find_entry(
    const struct devfs *filesystem,
    const struct device *device
);

static enum vfs_lookup_result devfs_materialize_character(
    struct devfs *filesystem,
    struct character_device *character,
    struct devfs_entry **result
);

static void devfs_release_registry_device(
    struct device *device
);

static enum vfs_stat_result devfs_root_stat(
    struct vfs_node *node,
    struct vfs_stat *result
);

static enum vfs_lookup_result devfs_root_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result
);

static struct devfs_directory_file *devfs_directory_file_from_vfs(
    struct vfs_file *file
);

static bool devfs_device_name_publishable(
    const struct device *device
);

static enum vfs_open_result devfs_root_open(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result
);

static enum vfs_directory_read_result devfs_directory_read(
    struct vfs_file *file,
    struct vfs_directory_entry *result
);

static void devfs_directory_file_destroy(
    struct vfs_file *file
);

// Static local variables
static const struct vfs_node_operations devfs_root_operations = {
    .open =
        devfs_root_open,
    .stat =
        devfs_root_stat,
    .lookup =
        devfs_root_lookup,
    .parent =
        NULL,
    .destroy =
        NULL,
};

static const struct vfs_file_operations
    devfs_directory_file_operations = {
        .read =
            NULL,
        .write =
            NULL,
        .read_directory =
            devfs_directory_read,
        .destroy =
            devfs_directory_file_destroy,
    };

// Public functions implementations
enum devfs_mount_result devfs_mount(
    const struct device_registry *registry,
    struct devfs **result)
{
    if (
        registry == NULL ||
        result == NULL
    ) {
        return
            DEVFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Iterator initialization gives devfs a public-contract way to verify
     * that the borrowed registry is currently initialized and structurally
     * valid without depending on registry.c private helpers.
     */
    struct device_registry_iterator iterator;

    if (!device_registry_iterator_initialize(
        registry,
        &iterator
    )) {
        return
            DEVFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    struct devfs *filesystem =
        kmalloc(sizeof(*filesystem));

    if (filesystem == NULL) {
        return
            DEVFS_MOUNT_RESULT_RESOURCE_EXHAUSTED;
    }

    filesystem->registry =
        registry;

    filesystem->entry_count =
        0;

    for (
        size_t index = 0;
        index < DEVICE_REGISTRY_CAPACITY;
        ++index
    ) {
        filesystem->entries[index] =
            NULL;
    }

    if (!vfs_node_initialize(
        &filesystem->root,
        VFS_NODE_TYPE_DIRECTORY,
        &devfs_root_operations,
        filesystem
    )) {
        kfree(
            filesystem
        );

        return
            DEVFS_MOUNT_RESULT_INVALID_ARGUMENT;
    }

    *result =
        filesystem;

    return
        DEVFS_MOUNT_RESULT_MOUNTED;
}

struct vfs_node *devfs_root(
    struct devfs *filesystem)
{
    if (
        filesystem == NULL ||
        filesystem->root.reference_count == 0 ||
        filesystem->root.type !=
            VFS_NODE_TYPE_DIRECTORY ||
        filesystem->root.private_data !=
            filesystem
    ) {
        return NULL;
    }

    return &filesystem->root;
}

bool devfs_unmount(
    struct devfs *filesystem)
{
    if (
        filesystem == NULL ||
        devfs_from_root(
            &filesystem->root
        ) != filesystem ||
        filesystem->root.reference_count != 1 ||
        filesystem->entry_count >
            DEVICE_REGISTRY_CAPACITY
    ) {
        return false;
    }

    /*
     * Perform a complete preflight before releasing anything. A rejected
     * unmount must leave the filesystem and all ownership unchanged.
     */
    for (
        size_t index = 0;
        index < filesystem->entry_count;
        ++index
    ) {
        struct devfs_entry *entry =
            filesystem->entries[index];

        if (
            entry == NULL ||
            entry->character.vfs.reference_count != 1 ||
            entry->character.character == NULL
        ) {
            return false;
        }
    }

    for (
        size_t index = 0;
        index < filesystem->entry_count;
        ++index
    ) {
        struct devfs_entry *entry =
            filesystem->entries[index];

        if (!vfs_node_release(
            &entry->character.vfs
        )) {
            kernel_panic(
                "devfs failed to release cached device vnode"
            );
        }

        kfree(
            entry
        );
    }

    if (!vfs_node_release(
        &filesystem->root
    )) {
        kernel_panic(
            "devfs failed to release root vnode"
        );
    }

    kfree(
        filesystem
    );

    return true;
}

// Private functions and helpers implementations
static struct devfs *devfs_from_root(
    struct vfs_node *node)
{
    if (
        node == NULL ||
        node->reference_count == 0 ||
        node->type !=
            VFS_NODE_TYPE_DIRECTORY ||
        node->private_data == NULL
    ) {
        return NULL;
    }

    struct devfs *filesystem =
        node->private_data;

    if (
        &filesystem->root != node ||
        filesystem->registry == NULL
    ) {
        return NULL;
    }

    return
        filesystem;
}

static struct devfs_directory_file *devfs_directory_file_from_vfs(
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

    struct devfs_directory_file *open_file =
        file->private_data;

    struct devfs *filesystem =
        devfs_from_root(
            file->node
        );

    if (
        filesystem == NULL ||
        &open_file->vfs !=
            file ||
        open_file->iterator.registry !=
            filesystem->registry
    ) {
        return NULL;
    }

    return
        open_file;
}

static bool devfs_device_name_publishable(
    const struct device *device)
{
    if (
        device == NULL ||
        device->name_length == 0 ||
        device->name_length >
            DEVICE_NAME_MAX ||
        device->name[
            device->name_length
        ] != '\0'
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < device->name_length;
        ++index
    ) {
        if (
            device->name[index] == '\0' ||
            device->name[index] == '/'
        ) {
            return false;
        }
    }

    if (
        device->name_length == 1 &&
        device->name[0] == '.'
    ) {
        return false;
    }

    if (
        device->name_length == 2 &&
        device->name[0] == '.' &&
        device->name[1] == '.'
    ) {
        return false;
    }

    return true;
}

static enum vfs_open_result devfs_root_open(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result)
{
    if (result == NULL) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    struct devfs *filesystem =
        devfs_from_root(
            node
        );

    if (filesystem == NULL) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (
        (access &
            VFS_OPEN_ACCESS_WRITE) != 0
    ) {
        return
            VFS_OPEN_RESULT_ACCESS_DENIED;
    }

    if (
        access !=
            VFS_OPEN_ACCESS_READ
    ) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    struct devfs_directory_file *open_file =
        kmalloc(sizeof(*open_file));

    if (open_file == NULL) {
        return
            VFS_OPEN_RESULT_RESOURCE_EXHAUSTED;
    }

    /*
     * Capture the registry generation before publishing the VFS file. The
     * iterator itself owns no resources, so failure remains transactional.
     */
    if (!device_registry_iterator_initialize(
        filesystem->registry,
        &open_file->iterator
    )) {
        kfree(
            open_file
        );

        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (!vfs_file_initialize(
        &open_file->vfs,
        node,
        access,
        &devfs_directory_file_operations,
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

static enum vfs_directory_read_result devfs_directory_read(
    struct vfs_file *file,
    struct vfs_directory_entry *result)
{
    if (result == NULL) {
        return
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
    }

    struct devfs_directory_file *open_file =
        devfs_directory_file_from_vfs(
            file
        );

    if (open_file == NULL) {
        return
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
    }

    for (;;) {
        struct device *device =
            NULL;

        enum device_registry_iteration_result iteration_result =
            device_registry_iterator_next(
                &open_file->iterator,
                &device
            );

        switch (iteration_result) {
            case DEVICE_REGISTRY_ITERATION_RESULT_END:
                return
                    VFS_DIRECTORY_READ_RESULT_END;

            case DEVICE_REGISTRY_ITERATION_RESULT_INVALIDATED:
                return
                    VFS_DIRECTORY_READ_RESULT_INVALIDATED;

            case DEVICE_REGISTRY_ITERATION_RESULT_INVALID_ARGUMENT:
                return
                    VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;

            case DEVICE_REGISTRY_ITERATION_RESULT_DEVICE:
                break;
        }

        if (device == NULL) {
            return
                VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
        }

        /*
         * The initial devfs namespace publishes character devices only.
         * Other classes remain present in the generic device registry but do
         * not consume a pathname.
         */
        if (
            device->device_class !=
                DEVICE_CLASS_CHARACTER ||
            !devfs_device_name_publishable(
                device
            )
        ) {
            devfs_release_registry_device(
                device
            );

            continue;
        }

        /*
         * A CHARACTER classification without a valid character capability is
         * a violated device-model invariant, not an invisible device.
         */
        if (
            character_device_from_device(
                device
            ) == NULL
        ) {
            devfs_release_registry_device(
                device
            );

            return
                VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
        }

        result->type =
            VFS_NODE_TYPE_CHARACTER_DEVICE;

        result->name_length =
            device->name_length;

        for (
            size_t index = 0;
            index < device->name_length;
            ++index
        ) {
            result->name[index] =
                device->name[index];
        }

        result->name[
            device->name_length
        ] = '\0';

        devfs_release_registry_device(
            device
        );

        return
            VFS_DIRECTORY_READ_RESULT_ENTRY;
    }
}

static void devfs_directory_file_destroy(
    struct vfs_file *file)
{
    if (
        file == NULL ||
        file->node == NULL
    ) {
        kernel_panic(
            "devfs directory file destroy received invalid file"
        );
    }

    struct devfs_directory_file *open_file =
        file->private_data;

    struct devfs *filesystem =
        devfs_from_root(
            file->node
        );

    if (
        open_file == NULL ||
        filesystem == NULL ||
        &open_file->vfs !=
            file ||
        open_file->iterator.registry !=
            filesystem->registry
    ) {
        kernel_panic(
            "devfs directory open-file ownership corrupted"
        );
    }

    /*
     * The iterator owns no registry reference. vfs_file_release() releases
     * the root vnode reference after this callback.
     */
    kfree(
        open_file
    );
}

static struct devfs_entry *devfs_find_entry(
    const struct devfs *filesystem,
    const struct device *device)
{
    if (
        filesystem == NULL ||
        device == NULL
    ) {
        return NULL;
    }

    for (
        size_t index = 0;
        index < filesystem->entry_count;
        ++index
    ) {
        struct devfs_entry *entry =
            filesystem->entries[index];

        if (
            entry != NULL &&
            entry->character.character != NULL &&
            entry->character.character->device ==
                device
        ) {
            return
                entry;
        }
    }

    return NULL;
}

static enum vfs_lookup_result devfs_materialize_character(
    struct devfs *filesystem,
    struct character_device *character,
    struct devfs_entry **result)
{
    if (
        filesystem == NULL ||
        character == NULL ||
        character->device == NULL ||
        result == NULL
    ) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    if (
        filesystem->entry_count >=
            DEVICE_REGISTRY_CAPACITY
    ) {
        return
            VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED;
    }

    struct devfs_entry *entry =
        kmalloc(sizeof(*entry));

    if (entry == NULL) {
        return
            VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED;
    }

    if (!vfs_character_device_node_initialize(
        &entry->character,
        character
    )) {
        kfree(
            entry
        );

        return
            VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED;
    }

    filesystem->entries[
        filesystem->entry_count
    ] = entry;

    ++filesystem->entry_count;

    *result =
        entry;

    return
        VFS_LOOKUP_RESULT_FOUND;
}

static void devfs_release_registry_device(
    struct device *device)
{
    if (
        device == NULL ||
        !device_release(
            device
        )
    ) {
        kernel_panic(
            "devfs failed to release registry device ownership"
        );
    }
}

static enum vfs_stat_result devfs_root_stat(
    struct vfs_node *node,
    struct vfs_stat *result)
{
    if (
        result == NULL ||
        devfs_from_root(
            node
        ) == NULL
    ) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    result->type =
        VFS_NODE_TYPE_DIRECTORY;

    result->size =
        0;

    return
        VFS_STAT_RESULT_SUCCESS;
}

static enum vfs_lookup_result devfs_root_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result)
{
    if (
        name == NULL ||
        result == NULL
    ) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    struct devfs *filesystem =
        devfs_from_root(
            directory
        );

    if (filesystem == NULL) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Device names are intentionally narrower than generic VFS pathname
     * components. A VFS-valid component that cannot be a device name is
     * absent from devfs rather than an invalid pathname.
     */
    if (
        name_length >
            DEVICE_NAME_MAX
    ) {
        return
            VFS_LOOKUP_RESULT_NOT_FOUND;
    }

    struct device *device =
        NULL;

    enum device_registry_lookup_result registry_result =
        device_registry_lookup_name(
            filesystem->registry,
            name,
            name_length,
            &device
        );

    if (
        registry_result ==
            DEVICE_REGISTRY_LOOKUP_RESULT_NOT_FOUND
    ) {
        return
            VFS_LOOKUP_RESULT_NOT_FOUND;
    }

    if (
        registry_result !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        device == NULL
    ) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    /*
     * The initial devfs publication policy exposes character devices only.
     * Other registered objects remain discoverable through the device model
     * but intentionally have no userspace pathname yet.
     */
    if (
        device->device_class !=
            DEVICE_CLASS_CHARACTER
    ) {
        devfs_release_registry_device(
            device
        );

        return
            VFS_LOOKUP_RESULT_NOT_FOUND;
    }

    struct devfs_entry *entry =
        devfs_find_entry(
            filesystem,
            device
        );

    if (entry == NULL) {
        struct character_device *character =
            character_device_from_device(
                device
            );

        if (character == NULL) {
            devfs_release_registry_device(
                device
            );

            return
                VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
        }

        enum vfs_lookup_result materialize_result =
            devfs_materialize_character(
                filesystem,
                character,
                &entry
            );

        if (
            materialize_result !=
                VFS_LOOKUP_RESULT_FOUND
        ) {
            devfs_release_registry_device(
                device
            );

            return
                materialize_result;
        }
    }

    /*
     * The cache now owns an independent generic-device reference through the
     * character-device VFS adapter. Drop the temporary registry lookup
     * ownership before returning the borrowed vnode.
     */
    devfs_release_registry_device(
        device
    );

    *result =
        &entry->character.vfs;

    return
        VFS_LOOKUP_RESULT_FOUND;
}

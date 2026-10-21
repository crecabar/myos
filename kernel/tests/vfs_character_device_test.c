// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file vfs_character_device_test.c
 * @brief Character-device VFS adapter regression tests.
 */

#include "vfs_character_device_test.h"

#include <myos/kernel/core/device/character_device.h>
#include <myos/kernel/core/device/registry.h>
#include <myos/kernel/core/device/device.h>
#include <myos/kernel/core/panic.h>
#include <myos/kernel/diagnostics/diagnostics.h>
#include "../drivers/pseudo.h"
#include "../vfs/character_device.h"
#include "../vfs/vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct vfs_would_block_probe {
    size_t read_calls;
    size_t readiness_calls;
    bool readable;
};

static enum character_device_io_result vfs_would_block_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    (void) buffer;
    (void) size;
    (void) bytes_read;

    struct vfs_would_block_probe *probe =
        character->private_data;

    if (probe == NULL) {
        kernel_panic("VFS WOULD_BLOCK probe missing");
    }

    ++probe->read_calls;

    return CHARACTER_DEVICE_IO_RESULT_WOULD_BLOCK;
}

static bool vfs_would_block_read_ready(
    struct character_device *character,
    bool *ready)
{
    if (
        character == NULL ||
        character->private_data == NULL ||
        ready == NULL
    ) {
        return false;
    }

    struct vfs_would_block_probe *probe =
        character->private_data;

    ++probe->readiness_calls;

    *ready = probe->readable;

    return true;
}

static const struct character_device_operations
    vfs_would_block_operations = {
        .read = vfs_would_block_read,
        .write = NULL,
        .read_ready = vfs_would_block_read_ready,
    };

struct vfs_read_wait_probe {
    struct vfs_file *file;
    size_t calls;
};

static void vfs_character_device_test_read_wait_notification(
    void *context);

// Private functions and helpers declarations
static void vfs_character_device_test_unregister(
    struct device_registry *registry,
    struct device *device
);

static void vfs_character_device_test_null(void);

static void vfs_character_device_test_zero(void);

static void vfs_character_device_test_would_block(void);

static void vfs_character_device_test_read_wait(void);

// Public functions implementations
void vfs_character_device_test_run(void)
{
    vfs_character_device_test_null();
    vfs_character_device_test_zero();

    vfs_character_device_test_would_block();
    vfs_character_device_test_read_wait();

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

static void vfs_character_device_test_read_wait_notification(
    void *context)
{
    struct vfs_read_wait_probe *probe = context;

    if (
        probe == NULL ||
        probe->file == NULL ||
        probe->file->reference_count == 0U
    ) {
        kernel_panic(
            "VFS read wait callback observed dead file"
        );
    }

    ++probe->calls;
}


static void vfs_character_device_test_read_wait(void)
{
    struct device generic;
    struct character_device character;
    struct vfs_character_device_node node;

    struct vfs_would_block_probe device_probe = {0};

    if (
        !device_initialize(
            &generic,
            6461,
            "vfs-read-wait",
            sizeof("vfs-read-wait") - 1U,
            DEVICE_KIND_PSEUDO,
            NULL,
            NULL
        ) ||
        !character_device_initialize(
            &character,
            &generic,
            &vfs_would_block_operations,
            &device_probe
        ) ||
        !vfs_character_device_node_initialize(
            &node,
            &character
        )
    ) {
        kernel_panic(
            "VFS read wait fixture initialization failed"
        );
    }

    struct vfs_file *first_file = NULL;
    struct vfs_file *second_file = NULL;

    if (
        vfs_node_open(
            &node.vfs,
            VFS_OPEN_ACCESS_READ,
            &first_file
        ) != VFS_OPEN_RESULT_OPENED ||
        first_file == NULL ||
        vfs_node_open(
            &node.vfs,
            VFS_OPEN_ACCESS_READ,
            &second_file
        ) != VFS_OPEN_RESULT_OPENED ||
        second_file == NULL ||
        first_file == second_file
    ) {
        kernel_panic(
            "VFS read wait independent opens failed"
        );
    }

    struct vfs_read_wait first = {0};
    struct vfs_read_wait second = {0};

    struct vfs_read_wait_probe a = {
        .file = first_file,
        .calls = 0U,
    };

    struct vfs_read_wait_probe b = {
        .file = second_file,
        .calls = 0U,
    };

    /*
     * Each wait acquires an additional file reference.
     * Both registrations share the same device queue.
     */
    if (
        vfs_file_read_wait_register(
            first_file,
            &first,
            vfs_character_device_test_read_wait_notification,
            &a
        ) != VFS_READINESS_RESULT_SUCCESS ||
        vfs_file_read_wait_register(
            second_file,
            &second,
            vfs_character_device_test_read_wait_notification,
            &b
        ) != VFS_READINESS_RESULT_SUCCESS ||
        first_file->reference_count != 2U ||
        second_file->reference_count != 2U ||
        node.vfs.reference_count != 3U ||
        first.entry.queue !=
            &character.read_waiters ||
        second.entry.queue !=
            &character.read_waiters ||
        device_probe.read_calls != 0U ||
        device_probe.readiness_calls != 0U
    ) {
        kernel_panic(
            "VFS read wait registration ownership failed"
        );
    }

    /*
     * The original open-file references are released.
     * Only the two active waits now keep files alive.
     */
    if (
        !vfs_file_release(first_file) ||
        !vfs_file_release(second_file) ||
        a.file->reference_count != 1U ||
        b.file->reference_count != 1U ||
        node.vfs.reference_count != 3U
    ) {
        kernel_panic(
            "VFS read waits did not retain open files"
        );
    }

    /*
     * Simulate availability on the shared device.
     * Each callback observes a still-live open file.
     */
    device_probe.readable = true;

    if (
        character_device_read_wait_notify(
            &character
        ) != 2U ||
        a.calls != 1U ||
        b.calls != 1U ||
        wait_queue_entry_registered(&first.entry) ||
        wait_queue_entry_registered(&second.entry) ||
        first.file != NULL ||
        second.file != NULL ||
        node.vfs.reference_count != 1U ||
        device_probe.read_calls != 0U ||
        device_probe.readiness_calls != 0U
    ) {
        kernel_panic(
            "VFS read wait notification ownership failed"
        );
    }

    /*
     * Delivery is one-shot. No pending registration
     * remains to cancel or deliver again.
     */
    if (
        vfs_read_wait_cancel(&first) ||
        vfs_read_wait_cancel(&second) ||
        character_device_read_wait_notify(
            &character
        ) != 0U ||
        a.calls != 1U ||
        b.calls != 1U
    ) {
        kernel_panic(
            "VFS read wait duplicated delivery"
        );
    }

    /*
     * Exercise cancellation after the original file
     * reference has already been released.
     */
    struct vfs_file *third_file = NULL;

    if (
        vfs_node_open(
            &node.vfs,
            VFS_OPEN_ACCESS_READ,
            &third_file
        ) != VFS_OPEN_RESULT_OPENED ||
        third_file == NULL
    ) {
        kernel_panic(
            "VFS read wait cancellation open failed"
        );
    }

    struct vfs_read_wait third = {0};

    struct vfs_read_wait_probe c = {
        .file = third_file,
        .calls = 0U,
    };

    if (
        vfs_file_read_wait_register(
            third_file,
            &third,
            vfs_character_device_test_read_wait_notification,
            &c
        ) != VFS_READINESS_RESULT_SUCCESS ||
        third_file->reference_count != 2U ||
        !vfs_file_release(third_file) ||
        c.file->reference_count != 1U ||
        !vfs_read_wait_cancel(&third) ||
        third.file != NULL ||
        wait_queue_entry_registered(&third.entry) ||
        node.vfs.reference_count != 1U ||
        character_device_read_wait_notify(
            &character
        ) != 0U ||
        c.calls != 0U
    ) {
        kernel_panic(
            "VFS read wait cancellation ownership failed"
        );
    }

    if (
        !vfs_node_release(&node.vfs) ||
        generic.reference_count != 1U ||
        !device_begin_removal(&generic) ||
        !device_finish_removal(&generic) ||
        !device_release(&generic)
    ) {
        kernel_panic(
            "VFS read wait fixture cleanup failed"
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

    /*
     * /dev/null does not yet expose read_ready.
     *
     * Unsupported capability must not be confused
     * with an empty readable stream.
     */
    bool ready = true;

    if (
        vfs_file_read_ready(
            file,
            &ready
        ) != VFS_READINESS_RESULT_NOT_SUPPORTED ||
        !ready
    ) {
        kernel_panic(
            "Character VFS unsupported readiness failed"
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

    struct vfs_read_wait unsupported_wait = {0};

    if (
        vfs_file_read_wait_register(
            file,
            &unsupported_wait,
            vfs_character_device_test_read_wait_notification,
            NULL
        ) != VFS_READINESS_RESULT_NOT_SUPPORTED ||
        unsupported_wait.file != NULL ||
        wait_queue_entry_registered(&unsupported_wait.entry)
    ) {
        kernel_panic(
            "VFS /dev/null unexpectedly supported read waits"
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

static void vfs_character_device_test_would_block(void)
{
    struct device generic;
    struct character_device character;
    struct vfs_character_device_node node;

    struct vfs_would_block_probe probe = {0};

    if (
        !device_initialize(
            &generic,
            6460,
            "vfs-would-block",
            sizeof("vfs-would-block") - 1U,
            DEVICE_KIND_PSEUDO,
            NULL,
            NULL
        ) ||
        !character_device_initialize(
            &character,
            &generic,
            &vfs_would_block_operations,
            &probe
        ) ||
        !vfs_character_device_node_initialize(
            &node,
            &character
        )
    ) {
        kernel_panic("VFS WOULD_BLOCK fixture setup failed");
    }

    struct vfs_file *file = NULL;

    if (
        vfs_node_open(
            &node.vfs,
            VFS_OPEN_ACCESS_READ,
            &file
        ) != VFS_OPEN_RESULT_OPENED ||
        file == NULL
    ) {
        kernel_panic("VFS WOULD_BLOCK open failed");
    }

    /*
     * An empty backend reports unreadable without
     * consuming input.
     */
    bool ready = true;

    if (
        vfs_file_read_ready(
            file,
            &ready
        ) != VFS_READINESS_RESULT_SUCCESS ||
        ready ||
        probe.readiness_calls != 1U ||
        probe.read_calls != 0U
    ) {
        kernel_panic(
            "Character VFS empty readiness query failed"
        );
    }

    uint8_t buffer[4] = {
        0x11U, 0x22U, 0x33U, 0x44U,
    };

    size_t bytes_read = SIZE_MAX;
    uint64_t original_offset = file->offset;

    if (
        vfs_file_read(
            file,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) != VFS_IO_RESULT_WOULD_BLOCK ||
        probe.read_calls != 1U ||
        bytes_read != SIZE_MAX ||
        file->offset != original_offset ||
        buffer[0] != 0x11U ||
        buffer[1] != 0x22U ||
        buffer[2] != 0x33U ||
        buffer[3] != 0x44U
    ) {
        kernel_panic("VFS WOULD_BLOCK propagation failed");
    }

    /*
     * Simulate a later availability transition.
     *
     * The readiness query must not execute the
     * device's read callback.
     */
    probe.readable = true;

    if (
        vfs_file_read_ready(
            file,
            &ready
        ) != VFS_READINESS_RESULT_SUCCESS ||
        !ready ||
        probe.readiness_calls != 2U ||
        probe.read_calls != 1U
    ) {
        kernel_panic(
            "Character VFS ready transition failed"
        );
    }

    /*
     * Zero-length reads succeed at the VFS layer
     * without invoking the device backend.
     */
    bytes_read = SIZE_MAX;

    if (
        vfs_file_read(
            file,
            NULL,
            0,
            &bytes_read
        ) != VFS_IO_RESULT_SUCCESS ||
        bytes_read != 0U ||
        probe.read_calls != 1U ||
        file->offset != original_offset
    ) {
        kernel_panic("VFS WOULD_BLOCK zero-length read failed");
    }

    if (
        !vfs_file_release(file) ||
        !vfs_node_release(&node.vfs) ||
        generic.reference_count != 1U ||
        !device_begin_removal(&generic) ||
        !device_finish_removal(&generic) ||
        !device_release(&generic)
    ) {
        kernel_panic("VFS WOULD_BLOCK fixture cleanup failed");
    }
}

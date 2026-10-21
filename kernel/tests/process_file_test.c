// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process_file_test.c
 * @brief Process descriptor-level file operation regression tests.
 */

#include "process_file_test.h"

#include <myos/kernel/arch/x86_64/interrupts.h>
#include <myos/kernel/core/panic.h>
#include <myos/kernel/diagnostics/diagnostics.h>
#include "../process/fd_table.h"
#include "../process/file.h"
#include "../process/lifecycle.h"
#include "../process/namespace.h"
#include "../scheduler/scheduler.h"
#include <myos/kernel/vfs/vfs.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

struct process_file_readiness_probe {
    enum vfs_readiness_result result;
    bool available;
    size_t calls;
};

struct process_file_wait_prepare_probe {
    struct wait_queue queue;

    enum vfs_readiness_result readiness_result;

    bool readable;
    bool become_readable_on_registration;

    size_t readiness_calls;
    size_t registration_calls;
};

static struct vfs_file *process_file_test_open_storage;
static size_t process_file_test_open_call_count;
static enum vfs_open_result process_file_test_open_result;

static size_t process_file_test_read_call_count;
static uint64_t process_file_test_read_last_offset;
static size_t process_file_test_read_transfer_size;
static enum vfs_io_result process_file_test_read_result;

static size_t process_file_test_write_call_count;
static uint64_t process_file_test_write_last_offset;
static size_t process_file_test_write_transfer_size;
static enum vfs_io_result process_file_test_write_result;

static size_t process_file_test_stat_call_count;
static uint64_t process_file_test_stat_size;
static enum vfs_stat_result process_file_test_stat_result;

static enum vfs_lookup_result process_file_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result
);

static enum vfs_open_result process_file_test_open_callback(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result
);

static enum vfs_io_result process_file_test_read_callback(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

static enum vfs_readiness_result process_file_test_read_ready_callback(
    struct vfs_file *file,
    bool *ready
);

static enum vfs_io_result process_file_test_write_callback(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

static enum vfs_stat_result process_file_test_stat_callback(
    struct vfs_node *node,
    struct vfs_stat *result
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

static void process_file_test_read_wait_termination(
    bool notify_before_termination,
    bool close_before_termination
);

static void process_file_test_open_close(void);

static void process_file_test_failures(void);

static void process_file_test_full_table_rollback(void);

static void process_file_test_io_success(void);

static void process_file_test_io_failures(void);

static void process_file_test_seek_stat_failures(void);

static void process_file_test_read_ready(void);

static void process_file_test_read_wait_scheduler_wakeup(void);

static const struct vfs_node_operations
    process_file_test_root_operations = {
        .lookup =
            process_file_test_lookup,
    };

static const struct vfs_node_operations
    process_file_test_file_operations = {
        .open =
            process_file_test_open_callback,
        .stat =
            process_file_test_stat_callback,
    };

static const struct vfs_file_operations
    process_file_test_io_operations = {
        .read =
            process_file_test_read_callback,
        .write =
            process_file_test_write_callback,
    };

static void process_file_test_would_block(void);

static enum vfs_readiness_result process_file_test_wait_readiness(
    struct vfs_file *file,
    bool *ready
);

static enum vfs_readiness_result process_file_test_wait_registration(
    struct vfs_file *file,
    struct wait_queue_entry *entry,
    wait_queue_notification_fn notification,
    void *context
);

static enum vfs_readiness_result process_file_test_prepare_readiness(
    struct vfs_file *file,
    bool *ready
);

static enum vfs_readiness_result process_file_test_prepare_registration(
    struct vfs_file *file,
    struct wait_queue_entry *entry,
    wait_queue_notification_fn notification,
    void *context
);

static void process_file_test_read_wait(void);

static void process_file_test_read_wait_prepare(void);

void process_file_test_run(void)
{
    process_file_test_open_close();
    process_file_test_io_success();
    process_file_test_io_failures();
    process_file_test_read_ready();
    process_file_test_read_wait();
    process_file_test_read_wait_scheduler_wakeup();
    process_file_test_read_wait_prepare();

    /*
     * Termination cancels a pending wait with an open FD.
     */
    process_file_test_read_wait_termination(
        false,
        false
    );

    /*
     * Termination clears an already-delivered request.
     */
    process_file_test_read_wait_termination(
        true,
        false
    );

    /*
     * Termination cancels a pending wait even after
     * the original descriptor has been closed.
     */
    process_file_test_read_wait_termination(
        false,
        true
    );

    /*
     * A delivered request also cleans up correctly
     * when its original descriptor was closed.
     */
    process_file_test_read_wait_termination(
        true,
        true
    );
    process_file_test_would_block();
    process_file_test_seek_stat_failures();
    process_file_test_failures();
    process_file_test_full_table_rollback();

    diagnostics_write(
        "[process] Descriptor file operation tests passed\n"
    );
}

static enum vfs_lookup_result process_file_test_lookup(
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
        *result =
            fixture->file;

        return
            VFS_LOOKUP_RESULT_FOUND;
    }

    return
        VFS_LOOKUP_RESULT_NOT_FOUND;
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
        &process_file_test_io_operations,
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

static enum vfs_io_result process_file_test_read_callback(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    ++process_file_test_read_call_count;

    process_file_test_read_last_offset =
        offset;

    if (
        file == NULL ||
        buffer == NULL ||
        size == 0 ||
        bytes_read == NULL
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        process_file_test_read_result !=
        VFS_IO_RESULT_SUCCESS
    ) {
        return
            process_file_test_read_result;
    }

    if (
        process_file_test_read_transfer_size >
        size
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    uint8_t *bytes =
        buffer;

    if (
        process_file_test_read_transfer_size != 0
    ) {
        bytes[0] =
            0xa5;
    }

    *bytes_read =
        process_file_test_read_transfer_size;

    return
        VFS_IO_RESULT_SUCCESS;
}

static enum vfs_readiness_result process_file_test_read_ready_callback(
    struct vfs_file *file,
    bool *ready)
{
    if (
        file == NULL ||
        ready == NULL ||
        file->private_data == NULL
    ) {
        return VFS_READINESS_RESULT_INVALID_ARGUMENT;
    }

    struct process_file_readiness_probe *probe =
        file->private_data;

    ++probe->calls;

    /*
     * Deliberately write the callback-local output even
     * on failure. The layers above must isolate it.
     */
    *ready = probe->available;

    return probe->result;
}

static enum vfs_io_result process_file_test_write_callback(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    ++process_file_test_write_call_count;

    process_file_test_write_last_offset =
        offset;

    if (
        file == NULL ||
        buffer == NULL ||
        size == 0 ||
        bytes_written == NULL
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        process_file_test_write_result !=
        VFS_IO_RESULT_SUCCESS
    ) {
        return
            process_file_test_write_result;
    }

    if (
        process_file_test_write_transfer_size >
        size
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    *bytes_written =
        process_file_test_write_transfer_size;

    return
        VFS_IO_RESULT_SUCCESS;
}

static enum vfs_stat_result process_file_test_stat_callback(
    struct vfs_node *node,
    struct vfs_stat *result)
{
    ++process_file_test_stat_call_count;

    if (
        node == NULL ||
        result == NULL
    ) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    result->type =
        node->type;

    result->size =
        process_file_test_stat_size;

    return
        process_file_test_stat_result;
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
            &fixture->root,
            NULL
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

    process_file_test_read_result =
        VFS_IO_RESULT_SUCCESS;

    process_file_test_write_result =
        VFS_IO_RESULT_SUCCESS;

    process_file_test_stat_result =
        VFS_STAT_RESULT_SUCCESS;

    process_file_test_read_call_count =
        0;

    process_file_test_read_last_offset =
        0;

    process_file_test_read_transfer_size =
        0;

    process_file_test_write_call_count =
        0;

    process_file_test_write_last_offset =
        0;

    process_file_test_write_transfer_size =
        0;

    process_file_test_stat_call_count =
        0;

    process_file_test_stat_size =
        0;
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

static void process_file_test_io_success(void)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(
        &fixture
    );

    process_file_test_read_transfer_size =
        3;

    process_file_test_write_transfer_size =
        2;

    process_file_test_stat_size =
        64;

    size_t descriptor;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0
    ) {
        kernel_panic(
            "Unable to open process I/O fixture"
        );
    }

    uint8_t read_buffer[8] = {0};

    size_t bytes_read =
        99;

    if (
        process_file_read(
            &fixture.instance,
            descriptor,
            read_buffer,
            sizeof(read_buffer),
            &bytes_read
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        bytes_read != 3 ||
        read_buffer[0] != 0xa5 ||
        fixture.file_storage.offset != 3 ||
        process_file_test_read_call_count != 1 ||
        process_file_test_read_last_offset != 0
    ) {
        kernel_panic(
            "Process descriptor short read contract failed"
        );
    }

    /*
     * Zero-length I/O is handled by generic VFS after access validation and
     * does not invoke the filesystem callback or advance the shared offset.
     */
    bytes_read =
        99;

    if (
        process_file_read(
            &fixture.instance,
            descriptor,
            NULL,
            0,
            &bytes_read
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        bytes_read != 0 ||
        fixture.file_storage.offset != 3 ||
        process_file_test_read_call_count != 1
    ) {
        kernel_panic(
            "Process descriptor zero-length read contract failed"
        );
    }

    const uint8_t write_buffer[5] = {
        1,
        2,
        3,
        4,
        5,
    };

    size_t bytes_written =
        99;

    if (
        process_file_write(
            &fixture.instance,
            descriptor,
            write_buffer,
            sizeof(write_buffer),
            &bytes_written
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        bytes_written != 2 ||
        fixture.file_storage.offset != 5 ||
        process_file_test_write_call_count != 1 ||
        process_file_test_write_last_offset != 3
    ) {
        kernel_panic(
            "Process descriptor short write contract failed"
        );
    }

    bytes_written =
        99;

    if (
        process_file_write(
            &fixture.instance,
            descriptor,
            NULL,
            0,
            &bytes_written
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        bytes_written != 0 ||
        fixture.file_storage.offset != 5 ||
        process_file_test_write_call_count != 1
    ) {
        kernel_panic(
            "Process descriptor zero-length write contract failed"
        );
    }

    uint64_t offset =
        UINT64_MAX;

    if (
        process_file_seek(
            &fixture.instance,
            descriptor,
            10,
            VFS_SEEK_ORIGIN_START,
            &offset
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        offset != 10 ||
        fixture.file_storage.offset != 10
    ) {
        kernel_panic(
            "Process descriptor SEEK_START failed"
        );
    }

    offset =
        UINT64_MAX;

    if (
        process_file_seek(
            &fixture.instance,
            descriptor,
            -4,
            VFS_SEEK_ORIGIN_CURRENT,
            &offset
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        offset != 6 ||
        fixture.file_storage.offset != 6
    ) {
        kernel_panic(
            "Process descriptor SEEK_CURRENT failed"
        );
    }

    struct vfs_stat metadata = {
        .type =
            VFS_NODE_TYPE_CHARACTER_DEVICE,
        .size =
            UINT64_MAX,
    };

    if (
        process_file_stat(
            &fixture.instance,
            descriptor,
            &metadata
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        metadata.type !=
            VFS_NODE_TYPE_REGULAR_FILE ||
        metadata.size != 64 ||
        process_file_test_stat_call_count != 1
    ) {
        kernel_panic(
            "Process descriptor stat contract failed"
        );
    }

    offset =
        UINT64_MAX;

    if (
        process_file_seek(
            &fixture.instance,
            descriptor,
            -4,
            VFS_SEEK_ORIGIN_END,
            &offset
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        offset != 60 ||
        fixture.file_storage.offset != 60 ||
        process_file_test_stat_call_count != 2
    ) {
        kernel_panic(
            "Process descriptor SEEK_END failed"
        );
    }

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to close process I/O fixture"
        );
    }

    process_file_test_expect_baseline(
        &fixture
    );

    process_file_test_fixture_release(
        &fixture
    );
}

static void process_file_test_io_failures(void)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(
        &fixture
    );

    size_t descriptor;

    /*
     * A read-only open must reject writes, including zero-length writes.
     * Access validation belongs to the open-file description and happens
     * before zero-length I/O succeeds.
     */
    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to open read-only process file fixture"
        );
    }

    const uint8_t write_byte =
        0x5a;

    size_t transferred =
        77;

    if (
        process_file_write(
            &fixture.instance,
            descriptor,
            &write_byte,
            1,
            &transferred
        ) != PROCESS_FILE_RESULT_ACCESS_DENIED ||
        transferred != 77 ||
        fixture.file_storage.offset != 0 ||
        process_file_test_write_call_count != 0
    ) {
        kernel_panic(
            "Process descriptor write ignored access mode"
        );
    }

    if (
        process_file_write(
            &fixture.instance,
            descriptor,
            NULL,
            0,
            &transferred
        ) != PROCESS_FILE_RESULT_ACCESS_DENIED ||
        transferred != 77 ||
        fixture.file_storage.offset != 0 ||
        process_file_test_write_call_count != 0
    ) {
        kernel_panic(
            "Process zero-length write bypassed access validation"
        );
    }

    uint8_t read_byte =
        0;

    if (
        process_file_read(
            &fixture.instance,
            descriptor,
            NULL,
            1,
            &transferred
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        transferred != 77 ||
        fixture.file_storage.offset != 0 ||
        process_file_test_read_call_count != 0
    ) {
        kernel_panic(
            "Process descriptor read accepted NULL buffer"
        );
    }

    if (
        process_file_read(
            NULL,
            descriptor,
            &read_byte,
            1,
            &transferred
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        transferred != 77 ||
        process_file_read(
            &fixture.instance,
            descriptor,
            &read_byte,
            1,
            NULL
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Process descriptor read accepted invalid arguments"
        );
    }

    if (
        process_file_read(
            &fixture.instance,
            PROCESS_FD_TABLE_CAPACITY,
            &read_byte,
            1,
            &transferred
        ) != PROCESS_FILE_RESULT_BAD_DESCRIPTOR ||
        transferred != 77
    ) {
        kernel_panic(
            "Process descriptor read accepted bad descriptor"
        );
    }

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to close read-only process file fixture"
        );
    }

    /*
     * Exercise the symmetric read denial on a write-only description.
     */
    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_WRITE,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to open write-only process file fixture"
        );
    }

    transferred =
        77;

    if (
        process_file_read(
            &fixture.instance,
            descriptor,
            &read_byte,
            1,
            &transferred
        ) != PROCESS_FILE_RESULT_ACCESS_DENIED ||
        transferred != 77 ||
        fixture.file_storage.offset != 0
    ) {
        kernel_panic(
            "Process descriptor read ignored access mode"
        );
    }

    if (
        process_file_read(
            &fixture.instance,
            descriptor,
            NULL,
            0,
            &transferred
        ) != PROCESS_FILE_RESULT_ACCESS_DENIED ||
        transferred != 77 ||
        fixture.file_storage.offset != 0
    ) {
        kernel_panic(
            "Process zero-length read bypassed access validation"
        );
    }

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to prepare process I/O failure fixture"
        );
    }

    /*
     * Missing file operations must remain distinguishable from an invalid
     * descriptor.
     */
    fixture.file_storage.operations =
        NULL;

    transferred =
        77;

    if (
        process_file_read(
            &fixture.instance,
            descriptor,
            &read_byte,
            1,
            &transferred
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        transferred != 77 ||
        fixture.file_storage.offset != 0 ||
        process_file_write(
            &fixture.instance,
            descriptor,
            &write_byte,
            1,
            &transferred
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        transferred != 77 ||
        fixture.file_storage.offset != 0
    ) {
        kernel_panic(
            "Process descriptor I/O mishandled missing operations"
        );
    }

    fixture.file_storage.operations =
        &process_file_test_io_operations;

    /*
     * VFS failures must map cleanly and must not publish process-level
     * transfer outputs or advance the shared offset.
     */
    process_file_test_read_result =
        VFS_IO_RESULT_RESOURCE_EXHAUSTED;

    transferred =
        77;

    if (
        process_file_read(
            &fixture.instance,
            descriptor,
            &read_byte,
            1,
            &transferred
        ) != PROCESS_FILE_RESULT_RESOURCE_EXHAUSTED ||
        transferred != 77 ||
        fixture.file_storage.offset != 0
    ) {
        kernel_panic(
            "Process descriptor read failure was not transactional"
        );
    }

    process_file_test_write_result =
        VFS_IO_RESULT_RESOURCE_EXHAUSTED;

    transferred =
        77;

    if (
        process_file_write(
            &fixture.instance,
            descriptor,
            &write_byte,
            1,
            &transferred
        ) != PROCESS_FILE_RESULT_RESOURCE_EXHAUSTED ||
        transferred != 77 ||
        fixture.file_storage.offset != 0
    ) {
        kernel_panic(
            "Process descriptor write failure was not transactional"
        );
    }

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to close process I/O failure fixture"
        );
    }

    process_file_test_expect_baseline(
        &fixture
    );

    process_file_test_fixture_release(
        &fixture
    );
}

static void process_file_test_seek_stat_failures(void)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(
        &fixture
    );

    process_file_test_stat_size =
        64;

    size_t descriptor;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1,
            VFS_OPEN_ACCESS_READ |
                VFS_OPEN_ACCESS_WRITE,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to open process seek/stat failure fixture"
        );
    }

    uint64_t offset =
        1234;

    if (
        process_file_seek(
            NULL,
            descriptor,
            0,
            VFS_SEEK_ORIGIN_START,
            &offset
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        offset != 1234 ||
        process_file_seek(
            &fixture.instance,
            descriptor,
            0,
            VFS_SEEK_ORIGIN_START,
            NULL
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Process descriptor seek accepted invalid arguments"
        );
    }

    if (
        process_file_seek(
            &fixture.instance,
            PROCESS_FD_TABLE_CAPACITY,
            0,
            VFS_SEEK_ORIGIN_START,
            &offset
        ) != PROCESS_FILE_RESULT_BAD_DESCRIPTOR ||
        offset != 1234
    ) {
        kernel_panic(
            "Process descriptor seek accepted bad descriptor"
        );
    }

    /*
     * Underflow and unknown origins must leave both the published result and
     * the shared open-file offset unchanged.
     */
    if (
        process_file_seek(
            &fixture.instance,
            descriptor,
            -1,
            VFS_SEEK_ORIGIN_START,
            &offset
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        offset != 1234 ||
        fixture.file_storage.offset != 0
    ) {
        kernel_panic(
            "Process descriptor seek underflow was not transactional"
        );
    }

    /*
     * The bounded process operation must reject an offset that the syscall ABI
     * cannot represent, without modifying shared open-file state.
     */
    fixture.file_storage.offset =
        (uint64_t) INT64_MAX;

    offset =
        1234;

    if (
        process_file_seek_bounded(
            &fixture.instance,
            descriptor,
            1,
            VFS_SEEK_ORIGIN_CURRENT,
            (uint64_t) INT64_MAX,
            &offset
        ) != PROCESS_FILE_RESULT_OVERFLOW ||
        offset != 1234 ||
        fixture.file_storage.offset !=
            (uint64_t) INT64_MAX
    ) {
        kernel_panic(
            "Process bounded seek overflow was not transactional"
        );
    }

    /*
     * The generic process API remains capable of representing the complete
     * uint64_t VFS offset range.
     */
    if (
        process_file_seek(
            &fixture.instance,
            descriptor,
            1,
            VFS_SEEK_ORIGIN_CURRENT,
            &offset
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        offset !=
            (uint64_t) INT64_MAX + 1ULL ||
        fixture.file_storage.offset !=
            (uint64_t) INT64_MAX + 1ULL
    ) {
        kernel_panic(
            "Process generic seek was incorrectly ABI-bounded"
        );
    }

    /*
     * Restore the fixture state expected by the remaining seek failure tests.
     */
    fixture.file_storage.offset =
        0;

    offset =
        1234;

    enum vfs_seek_origin invalid_origin =
        (enum vfs_seek_origin) 99;

    if (
        process_file_seek(
            &fixture.instance,
            descriptor,
            0,
            invalid_origin,
            &offset
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        offset != 1234 ||
        fixture.file_storage.offset != 0
    ) {
        kernel_panic(
            "Process descriptor seek accepted invalid origin"
        );
    }

    struct vfs_stat metadata = {
        .type =
            VFS_NODE_TYPE_CHARACTER_DEVICE,
        .size =
            UINT64_MAX,
    };

    if (
        process_file_stat(
            NULL,
            descriptor,
            &metadata
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        metadata.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        metadata.size != UINT64_MAX ||
        process_file_stat(
            &fixture.instance,
            descriptor,
            NULL
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Process descriptor stat accepted invalid arguments"
        );
    }

    if (
        process_file_stat(
            &fixture.instance,
            PROCESS_FD_TABLE_CAPACITY,
            &metadata
        ) != PROCESS_FILE_RESULT_BAD_DESCRIPTOR ||
        metadata.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        metadata.size != UINT64_MAX
    ) {
        kernel_panic(
            "Process descriptor stat accepted bad descriptor"
        );
    }

    /*
     * stat and SEEK_END both depend on the node metadata operation. Removing
     * it must produce NOT_SUPPORTED without mutating caller-visible state.
     */
    fixture.file_node.operations =
        NULL;

    if (
        process_file_stat(
            &fixture.instance,
            descriptor,
            &metadata
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        metadata.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        metadata.size != UINT64_MAX
    ) {
        kernel_panic(
            "Process descriptor stat mishandled unsupported metadata"
        );
    }

    if (
        process_file_seek(
            &fixture.instance,
            descriptor,
            0,
            VFS_SEEK_ORIGIN_END,
            &offset
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        offset != 1234 ||
        fixture.file_storage.offset != 0
    ) {
        kernel_panic(
            "Process descriptor SEEK_END mishandled unsupported metadata"
        );
    }

    fixture.file_node.operations =
        &process_file_test_file_operations;

    /*
     * A filesystem metadata failure must also remain transactional.
     */
    process_file_test_stat_result =
        VFS_STAT_RESULT_INVALID_ARGUMENT;

    enum process_file_result stat_failure_result =
        process_file_stat(
            &fixture.instance,
            descriptor,
            &metadata
        );

    if (
        stat_failure_result !=
        PROCESS_FILE_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Process descriptor stat failure mapped incorrectly"
        );
    }

    if (
        metadata.type !=
            VFS_NODE_TYPE_CHARACTER_DEVICE ||
        metadata.size != UINT64_MAX
    ) {
        kernel_panic(
            "Process descriptor stat failure published metadata"
        );
    }

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to close process seek/stat failure fixture"
        );
    }

    process_file_test_expect_baseline(
        &fixture
    );

    process_file_test_fixture_release(
        &fixture
    );
}


static void process_file_test_read_ready(void)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(&fixture);

    struct process_file_readiness_probe probe = {
        .result = VFS_READINESS_RESULT_SUCCESS,
        .available = false,
        .calls = 0U,
    };

    bool ready = true;

    /*
     * Invalid process, output and descriptor requests
     * must fail before reaching the VFS.
     */
    if (
        process_file_read_ready(
            NULL,
            0U,
            &ready
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        process_file_read_ready(
            &fixture.instance,
            0U,
            NULL
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        process_file_read_ready(
            &fixture.instance,
            PROCESS_FD_TABLE_CAPACITY,
            &ready
        ) != PROCESS_FILE_RESULT_BAD_DESCRIPTOR ||
        !ready ||
        probe.calls != 0U
    ) {
        kernel_panic(
            "Process readiness invalid request handling failed"
        );
    }

    size_t descriptor = SIZE_MAX;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1U,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0U
    ) {
        kernel_panic(
            "Process readiness fixture open failed"
        );
    }

    /*
     * Reuse the ordinary VFS fixture, replacing only
     * its per-open operations and private test state.
     */
    struct vfs_file_operations operations =
        process_file_test_io_operations;

    operations.read_ready =
        process_file_test_read_ready_callback;

    fixture.file_storage.operations = &operations;
    fixture.file_storage.private_data = &probe;
    fixture.file_storage.offset = 19U;

    /*
     * Supported capability with no available data.
     */
    if (
        process_file_read_ready(
            &fixture.instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        ready ||
        probe.calls != 1U ||
        process_file_test_read_call_count != 0U ||
        fixture.file_storage.offset != 19U
    ) {
        kernel_panic(
            "Process readiness empty query failed"
        );
    }

    /*
     * Available data must be reported without actually
     * executing the read callback.
     */
    probe.available = true;

    if (
        process_file_read_ready(
            &fixture.instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        !ready ||
        probe.calls != 2U ||
        process_file_test_read_call_count != 0U ||
        fixture.file_storage.offset != 19U
    ) {
        kernel_panic(
            "Process readiness available query failed"
        );
    }

    /*
     * Backend failures must preserve the caller's output.
     */
    probe.available = false;
    probe.result = VFS_READINESS_RESULT_NOT_SUPPORTED;

    if (
        process_file_read_ready(
            &fixture.instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        !ready ||
        probe.calls != 3U
    ) {
        kernel_panic(
            "Process readiness unsupported mapping failed"
        );
    }

    probe.result = VFS_READINESS_RESULT_INVALID_ARGUMENT;

    if (
        process_file_read_ready(
            &fixture.instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        !ready ||
        probe.calls != 4U
    ) {
        kernel_panic(
            "Process readiness invalid mapping failed"
        );
    }

    probe.result = VFS_READINESS_RESULT_ACCESS_DENIED;

    if (
        process_file_read_ready(
            &fixture.instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_ACCESS_DENIED ||
        !ready ||
        probe.calls != 5U
    ) {
        kernel_panic(
            "Process readiness denied mapping failed"
        );
    }

    /*
     * A missing readiness operation is distinct from
     * a successfully queried empty stream.
     */
    operations.read_ready = NULL;

    if (
        process_file_read_ready(
            &fixture.instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        !ready ||
        probe.calls != 5U ||
        fixture.file_storage.offset != 19U
    ) {
        kernel_panic(
            "Process readiness missing capability failed"
        );
    }

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Process readiness fixture close failed"
        );
    }

    /*
     * Once closed, the descriptor no longer identifies
     * a valid open-file description.
     */
    if (
        process_file_read_ready(
            &fixture.instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_BAD_DESCRIPTOR ||
        !ready ||
        probe.calls != 5U
    ) {
        kernel_panic(
            "Process readiness accepted closed descriptor"
        );
    }

    /*
     * Reopen with WRITE access to validate real descriptor
     * permissions, rather than mutating file.access manually.
     */
    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1U,
            VFS_OPEN_ACCESS_WRITE,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0U
    ) {
        kernel_panic(
            "Process readiness write-only open failed"
        );
    }

    operations.read_ready =
        process_file_test_read_ready_callback;

    fixture.file_storage.operations = &operations;
    fixture.file_storage.private_data = &probe;

    probe.result = VFS_READINESS_RESULT_SUCCESS;

    if (
        process_file_read_ready(
            &fixture.instance,
            descriptor,
            &ready
        ) != PROCESS_FILE_RESULT_ACCESS_DENIED ||
        !ready ||
        probe.calls != 5U ||
        fixture.file_storage.offset != 0U
    ) {
        kernel_panic(
            "Process readiness bypassed read permissions"
        );
    }

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Process readiness write-only close failed"
        );
    }

    process_file_test_expect_baseline(&fixture);
    process_file_test_fixture_release(&fixture);
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

static void process_file_test_would_block(void)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(&fixture);

    size_t descriptor = SIZE_MAX;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1U,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic("WOULD_BLOCK fixture open failed");
    }

    process_file_test_read_result =
        VFS_IO_RESULT_WOULD_BLOCK;

    uint8_t buffer[4] = {
        0xa1U, 0xa2U, 0xa3U, 0xa4U,
    };

    size_t bytes_read = SIZE_MAX;
    uint64_t original_offset = fixture.file_storage.offset;

    if (
        process_file_read(
            &fixture.instance,
            descriptor,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) != PROCESS_FILE_RESULT_WOULD_BLOCK ||
        bytes_read != SIZE_MAX ||
        fixture.file_storage.offset != original_offset ||
        process_file_test_read_call_count != 1U ||
        process_file_test_read_last_offset != original_offset ||
        buffer[0] != 0xa1U ||
        buffer[1] != 0xa2U ||
        buffer[2] != 0xa3U ||
        buffer[3] != 0xa4U
    ) {
        kernel_panic("Process WOULD_BLOCK contract failed");
    }

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic("WOULD_BLOCK fixture close failed");
    }

    process_file_test_expect_baseline(&fixture);
    process_file_test_fixture_release(&fixture);
}

static enum vfs_readiness_result process_file_test_wait_readiness(
    struct vfs_file *file,
    bool *ready)
{
    if (file == NULL || ready == NULL) {
        return VFS_READINESS_RESULT_INVALID_ARGUMENT;
    }

    *ready = false;

    return VFS_READINESS_RESULT_SUCCESS;
}

static enum vfs_readiness_result process_file_test_wait_registration(
    struct vfs_file *file,
    struct wait_queue_entry *entry,
    wait_queue_notification_fn notification,
    void *context)
{
    if (
        file == NULL ||
        file->private_data == NULL ||
        entry == NULL ||
        notification == NULL
    ) {
        return VFS_READINESS_RESULT_INVALID_ARGUMENT;
    }

    struct wait_queue *queue =
        file->private_data;

    if (!wait_queue_register(
        queue,
        entry,
        notification,
        context
    )) {
        return VFS_READINESS_RESULT_INVALID_ARGUMENT;
    }

    return VFS_READINESS_RESULT_SUCCESS;
}

static enum vfs_readiness_result process_file_test_prepare_readiness(
    struct vfs_file *file,
    bool *ready)
{
    if (
        file == NULL ||
        ready == NULL ||
        file->private_data == NULL
    ) {
        return VFS_READINESS_RESULT_INVALID_ARGUMENT;
    }

    struct process_file_wait_prepare_probe *probe =
        file->private_data;

    ++probe->readiness_calls;

    /*
     * Expose the current readiness state without
     * consuming data or changing the file offset.
     */
    *ready = probe->readable;

    return probe->readiness_result;
}

static enum vfs_readiness_result process_file_test_prepare_registration(
    struct vfs_file *file,
    struct wait_queue_entry *entry,
    wait_queue_notification_fn notification,
    void *context)
{
    if (
        file == NULL ||
        file->private_data == NULL ||
        entry == NULL ||
        notification == NULL
    ) {
        return VFS_READINESS_RESULT_INVALID_ARGUMENT;
    }

    struct process_file_wait_prepare_probe *probe =
        file->private_data;

    if (!wait_queue_register(
        &probe->queue,
        entry,
        notification,
        context
    )) {
        return VFS_READINESS_RESULT_INVALID_ARGUMENT;
    }

    ++probe->registration_calls;

    /*
     * Simulate a producer making data available
     * after registration, but before the caller's
     * subsequent readiness query.
     *
     * Do not dispatch a synchronous notification:
     * registration callbacks may not deliver events.
     */
    if (probe->become_readable_on_registration) {
        probe->readable = true;
    }

    return VFS_READINESS_RESULT_SUCCESS;
}

static void process_file_test_read_wait(void)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(&fixture);

    /*
     * This is a lightweight descriptor test fixture,
     * not an ELF process registered with the scheduler.
     *
     * Establish only the lifecycle identity needed
     * for process-owned read wait operations.
     */
    fixture.instance.process.instance =
        &fixture.instance;

    struct wait_queue queue;
    wait_queue_init(&queue);

    size_t descriptor = SIZE_MAX;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1U,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0U
    ) {
        kernel_panic(
            "Process read wait fixture open failed"
        );
    }

    struct vfs_file_operations operations =
        process_file_test_io_operations;

    operations.read_ready =
        process_file_test_wait_readiness;

    operations.read_wait_register =
        process_file_test_wait_registration;

    fixture.file_storage.operations = &operations;
    fixture.file_storage.private_data = &queue;

    /*
     * Invalid descriptors and missing capabilities
     * must not install an active process wait.
     */
    if (
        process_file_read_wait_register(
            NULL,
            descriptor
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        process_file_read_wait_register(
            &fixture.instance,
            PROCESS_FD_TABLE_CAPACITY
        ) != PROCESS_FILE_RESULT_BAD_DESCRIPTOR ||
        fixture.instance.read_wait_active
    ) {
        kernel_panic(
            "Process read wait accepted invalid request"
        );
    }

    operations.read_wait_register = NULL;

    if (
        process_file_read_wait_register(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        fixture.instance.read_wait_active ||
        fixture.file_storage.reference_count != 1U
    ) {
        kernel_panic(
            "Process read wait missing capability failed"
        );
    }

    operations.read_wait_register =
        process_file_test_wait_registration;

    /*
     * The child-wait and descriptor-read wait mechanisms
     * must not be active simultaneously.
     */
    fixture.instance.wait_active = true;

    if (
        process_file_read_wait_register(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Process read wait accepted existing child wait"
        );
    }

    fixture.instance.wait_active = false;

    /*
     * Registering acquires an independent open-file
     * reference and does not block the process.
     */
    if (
        process_file_read_wait_register(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        !fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified ||
        !wait_queue_entry_registered(
            &fixture.instance.read_wait.entry
        ) ||
        fixture.file_storage.reference_count != 2U ||
        fixture.file_node.reference_count != 2U ||
        fixture.instance.process.state !=
            PROCESS_STATE_READY
    ) {
        kernel_panic(
            "Process read wait registration failed"
        );
    }

    /*
     * A process owns at most one outstanding read wait.
     */
    if (
        process_file_read_wait_register(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        fixture.file_storage.reference_count != 2U
    ) {
        kernel_panic(
            "Process read wait accepted duplicate request"
        );
    }

    /*
     * Cancel before delivery.
     *
     * This must release the reference acquired by
     * the VFS registration.
     */
    if (
        !process_file_read_wait_cancel(
            &fixture.instance
        ) ||
        fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified ||
        fixture.instance.read_wait.file != NULL ||
        wait_queue_entry_registered(
            &fixture.instance.read_wait.entry
        ) ||
        fixture.file_storage.reference_count != 1U ||
        queue.head != NULL ||
        queue.tail != NULL ||
        process_file_read_wait_cancel(
            &fixture.instance
        )
    ) {
        kernel_panic(
            "Process read wait cancellation failed"
        );
    }

    /*
     * Register again and close the original FD.
     *
     * The wait must keep the open-file description
     * alive even after descriptor removal.
     */
    if (
        process_file_read_wait_register(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
            kernel_panic(
                "Process read wait close setup failed"
            );
    }

    if (
        fixture.file_storage.reference_count != 1U ||
        fixture.file_node.reference_count != 2U ||
        !fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified
    ) {
        kernel_panic(
            "Process read wait did not retain closed file"
        );
    }

    /*
     * Simulate the resource publishing readability.
     *
     * Delivery removes the VFS registration and records
     * the notification on the process, without touching
     * scheduler state.
     */
    if (
        wait_queue_notify_all(&queue) != 1U ||
        !fixture.instance.read_wait_active ||
        !fixture.instance.read_wait_notified ||
        fixture.instance.read_wait.file != NULL ||
        fixture.file_storage.reference_count != 0U ||
        fixture.file_node.reference_count != 1U ||
        fixture.instance.process.state !=
            PROCESS_STATE_READY
    ) {
        kernel_panic(
            "Process read wait delivery failed"
        );
    }

    /*
     * The logical request must be cleared even though
     * the VFS entry was already detached on delivery.
     */
    if (
        !process_file_read_wait_cancel(
            &fixture.instance
        ) ||
        fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified ||
        process_file_read_wait_cancel(
            &fixture.instance
        ) ||
        wait_queue_notify_all(&queue) != 0U ||
        queue.head != NULL ||
        queue.tail != NULL
    ) {
        kernel_panic(
            "Process delivered read wait cleanup failed"
        );
    }

    process_file_test_expect_baseline(&fixture);
    process_file_test_fixture_release(&fixture);
}

static void process_file_test_read_wait_prepare(void)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(&fixture);

    /*
     * Lightweight process fixture. No Ring-3 context
     * or scheduler registration is required.
     */
    fixture.instance.process.instance =
        &fixture.instance;

    fixture.instance.process.state =
        PROCESS_STATE_READY;

    struct process_file_wait_prepare_probe probe = {
        .readiness_result =
            VFS_READINESS_RESULT_SUCCESS,
        .readable = false,
        .become_readable_on_registration = false,
        .readiness_calls = 0U,
        .registration_calls = 0U,
    };

    wait_queue_init(&probe.queue);

    size_t descriptor = SIZE_MAX;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1U,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0U
    ) {
        kernel_panic(
            "Read wait preparation fixture open failed"
        );
    }

    struct vfs_file_operations operations =
        process_file_test_io_operations;

    operations.read_ready =
        process_file_test_prepare_readiness;

    operations.read_wait_register =
        process_file_test_prepare_registration;

    fixture.file_storage.operations = &operations;
    fixture.file_storage.private_data = &probe;
    fixture.file_storage.offset = 19U;

    /*
     * Registration, readiness recheck, cancellation
     * and the eventual block decision must be
     * serialized against event delivery.
     */
    interrupts_disable();

    /*
     * Invalid arguments must not modify the output
     * or acquire any VFS ownership.
     */
    bool should_block = false;

    if (
        process_file_read_wait_prepare(
            NULL,
            descriptor,
            &should_block
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        process_file_read_wait_prepare(
            &fixture.instance,
            descriptor,
            NULL
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        process_file_read_wait_prepare(
            &fixture.instance,
            PROCESS_FD_TABLE_CAPACITY,
            &should_block
        ) != PROCESS_FILE_RESULT_BAD_DESCRIPTOR ||
        should_block ||
        probe.registration_calls != 0U ||
        probe.readiness_calls != 0U ||
        fixture.instance.read_wait_active ||
        fixture.file_storage.reference_count != 1U
    ) {
        kernel_panic(
            "Read wait preparation accepted invalid arguments"
        );
    }

    /*
     * A file without the wait-registration capability
     * cannot enter a blocking wait.
     */
    operations.read_wait_register = NULL;

    if (
        process_file_read_wait_prepare(
            &fixture.instance,
            descriptor,
            &should_block
        ) != PROCESS_FILE_RESULT_NOT_SUPPORTED ||
        should_block ||
        probe.registration_calls != 0U ||
        probe.readiness_calls != 0U ||
        fixture.instance.read_wait_active ||
        fixture.file_storage.reference_count != 1U
    ) {
        kernel_panic(
            "Read wait preparation accepted missing capability"
        );
    }

    operations.read_wait_register =
        process_file_test_prepare_registration;

    /*
     * Case 1: the resource remains unreadable.
     *
     * The caller must retain an active wait and
     * receive permission to enter BLOCKED.
     */
    should_block = false;

    if (
        process_file_read_wait_prepare(
            &fixture.instance,
            descriptor,
            &should_block
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        !should_block ||
        !fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified ||
        !wait_queue_entry_registered(
            &fixture.instance.read_wait.entry
        ) ||
        fixture.file_storage.reference_count != 2U ||
        fixture.file_node.reference_count != 2U ||
        probe.registration_calls != 1U ||
        probe.readiness_calls != 1U ||
        process_file_test_read_call_count != 0U ||
        fixture.file_storage.offset != 19U
    ) {
        kernel_panic(
            "Unreadable resource failed wait preparation"
        );
    }

    /*
     * A second preparation must not replace an
     * existing process-owned request.
     */
    should_block = false;

    if (
        process_file_read_wait_prepare(
            &fixture.instance,
            descriptor,
            &should_block
        ) != PROCESS_FILE_RESULT_INVALID_ARGUMENT ||
        should_block ||
        !fixture.instance.read_wait_active ||
        fixture.file_storage.reference_count != 2U ||
        probe.registration_calls != 1U ||
        probe.readiness_calls != 1U
    ) {
        kernel_panic(
            "Read wait preparation accepted duplicate request"
        );
    }

    /*
     * Cancel the pending request.
     *
     * The queue entry and independent VFS reference
     * must both be released.
     */
    if (
        !process_file_read_wait_cancel(
            &fixture.instance
        ) ||
        fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified ||
        fixture.instance.read_wait.file != NULL ||
        wait_queue_entry_registered(
            &fixture.instance.read_wait.entry
        ) ||
        probe.queue.head != NULL ||
        probe.queue.tail != NULL ||
        fixture.file_storage.reference_count != 1U ||
        wait_queue_notify_all(&probe.queue) != 0U
    ) {
        kernel_panic(
            "Prepared read wait cancellation failed"
        );
    }

    /*
     * Case 2: data becomes available during
     * registration.
     *
     * The second readiness query must detect it
     * and cancel the registration before returning.
     */
    probe.readable = false;
    probe.become_readable_on_registration = true;

    should_block = true;

    if (
        process_file_read_wait_prepare(
            &fixture.instance,
            descriptor,
            &should_block
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        should_block ||
        !probe.readable ||
        fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified ||
        fixture.instance.read_wait.file != NULL ||
        wait_queue_entry_registered(
            &fixture.instance.read_wait.entry
        ) ||
        probe.queue.head != NULL ||
        probe.queue.tail != NULL ||
        fixture.file_storage.reference_count != 1U ||
        probe.registration_calls != 2U ||
        probe.readiness_calls != 2U ||
        process_file_test_read_call_count != 0U ||
        fixture.file_storage.offset != 19U
    ) {
        kernel_panic(
            "Read wait recheck failed to detect available data"
        );
    }

    /*
     * Case 3: the backend readiness query fails
     * after registration succeeded.
     *
     * The error must propagate and every resource
     * acquired by registration must be rolled back.
     */
    probe.readable = false;
    probe.become_readable_on_registration = false;

    probe.readiness_result =
        VFS_READINESS_RESULT_ACCESS_DENIED;

    should_block = true;

    if (
        process_file_read_wait_prepare(
            &fixture.instance,
            descriptor,
            &should_block
        ) != PROCESS_FILE_RESULT_ACCESS_DENIED ||
        !should_block ||
        fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified ||
        fixture.instance.read_wait.file != NULL ||
        wait_queue_entry_registered(
            &fixture.instance.read_wait.entry
        ) ||
        probe.queue.head != NULL ||
        probe.queue.tail != NULL ||
        fixture.file_storage.reference_count != 1U ||
        probe.registration_calls != 3U ||
        probe.readiness_calls != 3U ||
        process_file_test_read_call_count != 0U ||
        fixture.file_storage.offset != 19U ||
        wait_queue_notify_all(&probe.queue) != 0U
    ) {
        kernel_panic(
            "Read wait readiness failure leaked ownership"
        );
    }

    /*
     * Preparation never changes process scheduling
     * state. It only decides whether blocking would
     * be valid for the caller.
     */
    if (
        fixture.instance.process.state !=
            PROCESS_STATE_READY
    ) {
        kernel_panic(
            "Read wait preparation modified process state"
        );
    }

    interrupts_enable();

    if (
        process_file_close(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        fixture.file_storage.reference_count != 0U
    ) {
        kernel_panic(
            "Read wait preparation fixture close failed"
        );
    }

    process_file_test_expect_baseline(&fixture);
    process_file_test_fixture_release(&fixture);
}

static void process_file_test_read_wait_termination(
    bool notify_before_termination,
    bool close_before_termination)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(&fixture);

    /*
     * Lightweight lifecycle fixture.
     *
     * The instance is not a scheduled ELF process,
     * but its descriptor and VFS ownership are real.
     */
    fixture.instance.process.instance =
        &fixture.instance;

    fixture.instance.process.state =
        PROCESS_STATE_READY;

    struct wait_queue queue;
    wait_queue_init(&queue);

    size_t descriptor = SIZE_MAX;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1U,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0U
    ) {
        kernel_panic(
            "Read wait termination fixture open failed"
        );
    }

    struct vfs_file_operations operations =
        process_file_test_io_operations;

    operations.read_ready =
        process_file_test_wait_readiness;

    operations.read_wait_register =
        process_file_test_wait_registration;

    fixture.file_storage.operations = &operations;
    fixture.file_storage.private_data = &queue;

    if (
        process_file_read_wait_register(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        !fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified ||
        fixture.file_storage.reference_count != 2U
    ) {
        kernel_panic(
            "Read wait termination registration failed"
        );
    }

    /*
     * Optionally deliver the event before termination.
     *
     * The VFS registration is removed, but the logical
     * process request remains active until cancellation.
     */
    if (notify_before_termination) {
        if (
            wait_queue_notify_all(&queue) != 1U ||
            !fixture.instance.read_wait_active ||
            !fixture.instance.read_wait_notified
        ) {
            kernel_panic(
                "Read wait termination notification failed"
            );
        }
    }

    /*
     * Optionally remove the descriptor first.
     *
     * A pending wait must survive this close because
     * it owns an independent VFS file reference.
     */
    if (close_before_termination) {
        if (
            process_file_close(
                &fixture.instance,
                descriptor
            ) != PROCESS_FILE_RESULT_SUCCESS
        ) {
            kernel_panic(
                "Read wait termination descriptor close failed"
            );
        }
    }

    /*
     * Simulate scheduler detachment and termination.
     *
     * The lifecycle hook must cancel the read request
     * before releasing any remaining descriptors.
     */
    fixture.instance.process.state =
        PROCESS_STATE_TERMINATED;

    fixture.instance.process.termination_reason =
        PROCESS_TERMINATION_EXITED;

    fixture.instance.process.exit_status = 0U;

    process_lifecycle_notify_terminated(
        &fixture.instance.process
    );

    if (
        fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified ||
        fixture.instance.read_wait.file != NULL ||
        wait_queue_entry_registered(
            &fixture.instance.read_wait.entry
        ) ||
        queue.head != NULL ||
        queue.tail != NULL ||
        fixture.file_storage.reference_count != 0U ||
        fixture.file_node.reference_count != 1U ||
        fixture.root.reference_count != 1U ||
        process_fd_table_get(
            &fixture.instance.file_descriptors,
            descriptor
        ) != NULL ||
        wait_queue_notify_all(&queue) != 0U ||
        process_file_read_wait_cancel(
            &fixture.instance
        )
    ) {
        kernel_panic(
            "Terminated process leaked read wait ownership"
        );
    }

    /*
     * The lifecycle hook already released descriptor
     * and namespace ownership.
     *
     * The fixture cleanup releases only its remaining
     * caller-owned VFS nodes.
     */
    process_file_test_fixture_release(&fixture);
}

static void process_file_test_read_wait_scheduler_wakeup(void)
{
    struct process_file_test_fixture fixture;

    process_file_test_fixture_initialize(&fixture);

    fixture.instance.process.instance =
        &fixture.instance;

    fixture.instance.process.state =
        PROCESS_STATE_READY;

    struct wait_queue queue;
    wait_queue_init(&queue);

    size_t descriptor = SIZE_MAX;

    if (
        process_file_open(
            &fixture.instance,
            "/file",
            sizeof("/file") - 1U,
            VFS_OPEN_ACCESS_READ,
            &descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS ||
        descriptor != 0U
    ) {
        kernel_panic(
            "Read wakeup fixture open failed"
        );
    }

    struct vfs_file_operations operations =
        process_file_test_io_operations;

    operations.read_ready =
        process_file_test_wait_readiness;

    operations.read_wait_register =
        process_file_test_wait_registration;

    fixture.file_storage.operations = &operations;
    fixture.file_storage.private_data = &queue;

    size_t process_count_before =
        scheduler_test_process_count();

    /*
     * Serialize registration, the synthetic BLOCKED
     * transition, and event delivery.
     *
     * The real syscall path will own the transition
     * to BLOCKED in a later slice.
     */
    interrupts_disable();

    if (
        !scheduler_add(
            &fixture.instance.process
        ) ||
        process_file_read_wait_register(
            &fixture.instance,
            descriptor
        ) != PROCESS_FILE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Read wakeup registration failed"
        );
    }

    fixture.instance.process.state =
        PROCESS_STATE_BLOCKED;

    /*
     * Delivery must wake the registered process.
     *
     * It must retain the logical request while
     * releasing the VFS-owned file reference.
     */
    if (
        wait_queue_notify_all(&queue) != 1U ||
        fixture.instance.process.state !=
            PROCESS_STATE_READY ||
        !fixture.instance.read_wait_active ||
        !fixture.instance.read_wait_notified ||
        fixture.instance.read_wait.file != NULL ||
        wait_queue_entry_registered(
            &fixture.instance.read_wait.entry
        ) ||
        fixture.file_storage.reference_count != 1U ||
        queue.head != NULL ||
        queue.tail != NULL
    ) {
        kernel_panic(
            "Read notification failed to wake BLOCKED process"
        );
    }

    /*
     * Clearing the delivered request must not
     * change scheduler state.
     */
    if (
        !process_file_read_wait_cancel(
            &fixture.instance
        ) ||
        fixture.instance.read_wait_active ||
        fixture.instance.read_wait_notified ||
        fixture.instance.process.state !=
            PROCESS_STATE_READY ||
        wait_queue_notify_all(&queue) != 0U
    ) {
        kernel_panic(
            "Read wakeup logical cleanup failed"
        );
    }

    /*
     * Detach the synthetic process before releasing
     * its descriptor and namespace resources.
     */
    fixture.instance.process.state =
        PROCESS_STATE_TERMINATED;

    fixture.instance.process.termination_reason =
        PROCESS_TERMINATION_EXITED;

    fixture.instance.process.exit_status = 0U;

    if (
        !scheduler_unregister_terminated(
            &fixture.instance.process
        ) ||
        scheduler_test_process_count() !=
            process_count_before
    ) {
        kernel_panic(
            "Read wakeup scheduler cleanup failed"
        );
    }

    process_lifecycle_notify_terminated(
        &fixture.instance.process
    );

    interrupts_enable();

    if (
        fixture.file_storage.reference_count != 0U ||
        fixture.file_node.reference_count != 1U ||
        fixture.root.reference_count != 1U
    ) {
        kernel_panic(
            "Read wakeup leaked VFS ownership"
        );
    }

    process_file_test_fixture_release(&fixture);
}

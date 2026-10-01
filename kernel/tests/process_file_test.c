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

static enum vfs_io_result process_file_test_read_callback(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read
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

static void process_file_test_open_close(void);

static void process_file_test_failures(void);

static void process_file_test_full_table_rollback(void);

static void process_file_test_io_success(void);

static void process_file_test_io_failures(void);

static void process_file_test_seek_stat_failures(void);

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

void process_file_test_run(void)
{
    process_file_test_open_close();
    process_file_test_io_success();
    process_file_test_io_failures();
    process_file_test_seek_stat_failures();
    process_file_test_failures();
    process_file_test_full_table_rollback();

    diagnostics_write(
        "[process] Descriptor file operation tests passed\n"
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

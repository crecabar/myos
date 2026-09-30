// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file vfs_file_io_test.c
 * @brief VFS open-file I/O regression tests.
 */

#include "vfs_file_io_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../vfs/vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Static local variables
static size_t vfs_file_io_test_read_call_count;
static uint64_t vfs_file_io_test_read_last_offset;
static size_t vfs_file_io_test_read_transfer_size;
static enum vfs_io_result vfs_file_io_test_read_result;
static bool vfs_file_io_test_read_mutate_offset;

static size_t vfs_file_io_test_write_call_count;
static uint64_t vfs_file_io_test_write_last_offset;
static size_t vfs_file_io_test_write_transfer_size;
static enum vfs_io_result vfs_file_io_test_write_result;
static bool vfs_file_io_test_write_mutate_offset;

// Private functions and helpers declarations
static enum vfs_io_result vfs_file_io_test_read_callback(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

static enum vfs_io_result vfs_file_io_test_write_callback(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

static void vfs_file_io_test_read_success(void);

static void vfs_file_io_test_read_validation(void);

static void vfs_file_io_test_read_callback_failures(void);

static void vfs_file_io_test_write_success(void);

static void vfs_file_io_test_write_validation(void);

static void vfs_file_io_test_write_callback_failures(void);

// Public functions implementations
void vfs_file_io_test_run(void)
{
    vfs_file_io_test_read_success();
    vfs_file_io_test_read_validation();
    vfs_file_io_test_read_callback_failures();

    vfs_file_io_test_write_success();
    vfs_file_io_test_write_validation();
    vfs_file_io_test_write_callback_failures();

    diagnostics_write(
        "[vfs] Open-file I/O contract tests passed\n"
    );
}

// Private functions and helpers implementations
static enum vfs_io_result vfs_file_io_test_read_callback(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    ++vfs_file_io_test_read_call_count;

    vfs_file_io_test_read_last_offset =
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

    if (vfs_file_io_test_read_mutate_offset) {
        ++file->offset;
    }

    /*
     * Some failure regressions deliberately configure this callback to
     * violate its output contract. The generic VFS must still isolate the
     * caller's output because it supplies callback-local storage.
     */
    *bytes_read =
        vfs_file_io_test_read_transfer_size;

    return
        vfs_file_io_test_read_result;
}

static enum vfs_io_result vfs_file_io_test_write_callback(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    ++vfs_file_io_test_write_call_count;

    vfs_file_io_test_write_last_offset =
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

    if (vfs_file_io_test_write_mutate_offset) {
        ++file->offset;
    }

    /*
     * Failure regressions may deliberately violate the callback output
     * contract. The generic VFS supplies callback-local storage, so the
     * caller's bytes_written must remain isolated.
     */
    *bytes_written =
        vfs_file_io_test_write_transfer_size;

    return
        vfs_file_io_test_write_result;
}

static void vfs_file_io_test_read_success(void)
{
    struct vfs_node node;
    struct vfs_file file;

    struct vfs_file_operations operations = {
        .read =
            vfs_file_io_test_read_callback,
    };

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
        )
    ) {
        kernel_panic(
            "VFS read success fixture initialization failed"
        );
    }

    file.offset =
        11;

    vfs_file_io_test_read_call_count =
        0;

    vfs_file_io_test_read_last_offset =
        0;

    vfs_file_io_test_read_transfer_size =
        3;

    vfs_file_io_test_read_result =
        VFS_IO_RESULT_SUCCESS;

    vfs_file_io_test_read_mutate_offset =
        false;

    uint8_t buffer[8] = {0};

    size_t bytes_read =
        99;

    if (
        vfs_file_read(
            &file,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) != VFS_IO_RESULT_SUCCESS ||
        bytes_read != 3 ||
        file.offset != 14 ||
        vfs_file_io_test_read_call_count != 1 ||
        vfs_file_io_test_read_last_offset != 11
    ) {
        kernel_panic(
            "VFS short read contract failed"
        );
    }

    /*
     * Zero-length I/O validates the file and access mode, but does not require
     * a buffer and must not invoke the filesystem callback.
     */
    bytes_read =
        99;

    if (
        vfs_file_read(
            &file,
            NULL,
            0,
            &bytes_read
        ) != VFS_IO_RESULT_SUCCESS ||
        bytes_read != 0 ||
        file.offset != 14 ||
        vfs_file_io_test_read_call_count != 1
    ) {
        kernel_panic(
            "VFS zero-length read contract failed"
        );
    }

    if (
        !vfs_file_release(&file) ||
        !vfs_node_release(&node)
    ) {
        kernel_panic(
            "VFS read success fixture cleanup failed"
        );
    }
}

static void vfs_file_io_test_read_validation(void)
{
    struct vfs_node node;
    struct vfs_file file;

    struct vfs_file_operations operations = {
        .read =
            vfs_file_io_test_read_callback,
    };

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
            VFS_OPEN_ACCESS_WRITE,
            &operations,
            NULL
        )
    ) {
        kernel_panic(
            "VFS read validation fixture initialization failed"
        );
    }

    file.offset =
        21;

    vfs_file_io_test_read_call_count =
        0;

    uint8_t buffer =
        0;

    size_t bytes_read =
        77;

    if (
        vfs_file_read(
            &file,
            &buffer,
            1,
            &bytes_read
        ) != VFS_IO_RESULT_ACCESS_DENIED ||
        bytes_read != 77 ||
        file.offset != 21 ||
        vfs_file_io_test_read_call_count != 0
    ) {
        kernel_panic(
            "VFS read ignored open access mode"
        );
    }

    /*
     * Access validation precedes zero-length handling.
     */
    if (
        vfs_file_read(
            &file,
            NULL,
            0,
            &bytes_read
        ) != VFS_IO_RESULT_ACCESS_DENIED ||
        bytes_read != 77 ||
        file.offset != 21 ||
        vfs_file_io_test_read_call_count != 0
    ) {
        kernel_panic(
            "VFS zero-length read bypassed access validation"
        );
    }

    file.access =
        VFS_OPEN_ACCESS_READ;

    if (
        vfs_file_read(
            &file,
            NULL,
            1,
            &bytes_read
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_read != 77 ||
        file.offset != 21 ||
        vfs_file_io_test_read_call_count != 0
    ) {
        kernel_panic(
            "VFS read accepted NULL non-empty buffer"
        );
    }

    if (
        vfs_file_read(
            NULL,
            &buffer,
            1,
            &bytes_read
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_read != 77 ||
        vfs_file_io_test_read_call_count != 0
    ) {
        kernel_panic(
            "VFS read accepted NULL file"
        );
    }

    if (
        vfs_file_read(
            &file,
            &buffer,
            1,
            NULL
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        file.offset != 21 ||
        vfs_file_io_test_read_call_count != 0
    ) {
        kernel_panic(
            "VFS read accepted NULL result storage"
        );
    }

    file.operations =
        NULL;

    if (
        vfs_file_read(
            &file,
            &buffer,
            1,
            &bytes_read
        ) != VFS_IO_RESULT_NOT_SUPPORTED ||
        bytes_read != 77 ||
        file.offset != 21 ||
        vfs_file_io_test_read_call_count != 0
    ) {
        kernel_panic(
            "VFS read accepted missing operation"
        );
    }

    file.operations =
        &operations;

    file.reference_count =
        0;

    if (
        vfs_file_read(
            &file,
            &buffer,
            1,
            &bytes_read
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_read != 77 ||
        file.offset != 21 ||
        vfs_file_io_test_read_call_count != 0
    ) {
        kernel_panic(
            "VFS read accepted dead file"
        );
    }

    file.reference_count =
        1;

    if (
        !vfs_file_release(&file) ||
        !vfs_node_release(&node)
    ) {
        kernel_panic(
            "VFS read validation fixture cleanup failed"
        );
    }
}

static void vfs_file_io_test_read_callback_failures(void)
{
    struct vfs_node node;
    struct vfs_file file;

    struct vfs_file_operations operations = {
        .read =
            vfs_file_io_test_read_callback,
    };

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
        )
    ) {
        kernel_panic(
            "VFS read failure fixture initialization failed"
        );
    }

    uint8_t buffer[8] = {0};

    size_t bytes_read =
        73;

    /*
     * A normal callback failure must preserve generic offset and caller
     * output, even if the callback writes its local bytes_read storage.
     */
    file.offset =
        40;

    vfs_file_io_test_read_call_count =
        0;

    vfs_file_io_test_read_transfer_size =
        5;

    vfs_file_io_test_read_result =
        VFS_IO_RESULT_NOT_SUPPORTED;

    vfs_file_io_test_read_mutate_offset =
        false;

    if (
        vfs_file_read(
            &file,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) != VFS_IO_RESULT_NOT_SUPPORTED ||
        bytes_read != 73 ||
        file.offset != 40 ||
        vfs_file_io_test_read_call_count != 1
    ) {
        kernel_panic(
            "VFS read callback failure mutated generic state"
        );
    }

    /*
     * The callback does not own the shared open-file offset.
     */
    vfs_file_io_test_read_transfer_size =
        1;

    vfs_file_io_test_read_result =
        VFS_IO_RESULT_SUCCESS;

    vfs_file_io_test_read_mutate_offset =
        true;

    if (
        vfs_file_read(
            &file,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_read != 73 ||
        file.offset != 40
    ) {
        kernel_panic(
            "VFS read accepted callback offset mutation"
        );
    }

    vfs_file_io_test_read_mutate_offset =
        false;

    /*
     * Successful callbacks cannot claim more bytes than were requested.
     */
    vfs_file_io_test_read_transfer_size =
        sizeof(buffer) + 1;

    if (
        vfs_file_read(
            &file,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_read != 73 ||
        file.offset != 40
    ) {
        kernel_panic(
            "VFS read accepted oversized transfer"
        );
    }

    /*
     * Offset advancement must not wrap uint64_t.
     */
    file.offset =
        UINT64_MAX - 1;

    vfs_file_io_test_read_transfer_size =
        2;

    if (
        vfs_file_read(
            &file,
            buffer,
            2,
            &bytes_read
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_read != 73 ||
        file.offset != UINT64_MAX - 1
    ) {
        kernel_panic(
            "VFS read accepted offset overflow"
        );
    }

    /*
     * Unknown callback result values must not escape the generic VFS.
     */
    file.offset =
        40;

    vfs_file_io_test_read_transfer_size =
        1;

    vfs_file_io_test_read_result =
        (enum vfs_io_result) 99;

    if (
        vfs_file_read(
            &file,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_read != 73 ||
        file.offset != 40
    ) {
        kernel_panic(
            "VFS read propagated unknown callback result"
        );
    }

    if (
        !vfs_file_release(&file) ||
        !vfs_node_release(&node)
    ) {
        kernel_panic(
            "VFS read failure fixture cleanup failed"
        );
    }
}

static void vfs_file_io_test_write_success(void)
{
    struct vfs_node node;
    struct vfs_file file;

    struct vfs_file_operations operations = {
        .write =
            vfs_file_io_test_write_callback,
    };

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
            &operations,
            NULL
        )
    ) {
        kernel_panic(
            "VFS write success fixture initialization failed"
        );
    }

    file.offset =
        23;

    vfs_file_io_test_write_call_count =
        0;

    vfs_file_io_test_write_last_offset =
        0;

    vfs_file_io_test_write_transfer_size =
        4;

    vfs_file_io_test_write_result =
        VFS_IO_RESULT_SUCCESS;

    vfs_file_io_test_write_mutate_offset =
        false;

    uint8_t buffer[8] = {0};

    size_t bytes_written =
        99;

    if (
        vfs_file_write(
            &file,
            buffer,
            sizeof(buffer),
            &bytes_written
        ) != VFS_IO_RESULT_SUCCESS ||
        bytes_written != 4 ||
        file.offset != 27 ||
        vfs_file_io_test_write_call_count != 1 ||
        vfs_file_io_test_write_last_offset != 23
    ) {
        kernel_panic(
            "VFS short write contract failed"
        );
    }

    /*
     * A write-only open-file description must also support successful writes.
     */
    file.access =
        VFS_OPEN_ACCESS_WRITE;

    vfs_file_io_test_write_transfer_size =
        1;

    bytes_written =
        99;

    if (
        vfs_file_write(
            &file,
            buffer,
            1,
            &bytes_written
        ) != VFS_IO_RESULT_SUCCESS ||
        bytes_written != 1 ||
        file.offset != 28 ||
        vfs_file_io_test_write_call_count != 2 ||
        vfs_file_io_test_write_last_offset != 27
    ) {
        kernel_panic(
            "VFS write-only access contract failed"
        );
    }

    /*
     * Zero-length I/O succeeds after access validation without invoking the
     * filesystem callback or requiring a buffer.
     */
    bytes_written =
        99;

    if (
        vfs_file_write(
            &file,
            NULL,
            0,
            &bytes_written
        ) != VFS_IO_RESULT_SUCCESS ||
        bytes_written != 0 ||
        file.offset != 28 ||
        vfs_file_io_test_write_call_count != 2
    ) {
        kernel_panic(
            "VFS zero-length write contract failed"
        );
    }

    if (
        !vfs_file_release(&file) ||
        !vfs_node_release(&node)
    ) {
        kernel_panic(
            "VFS write success fixture cleanup failed"
        );
    }
}

static void vfs_file_io_test_write_validation(void)
{
    struct vfs_node node;
    struct vfs_file file;

    struct vfs_file_operations operations = {
        .write =
            vfs_file_io_test_write_callback,
    };

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
        )
    ) {
        kernel_panic(
            "VFS write validation fixture initialization failed"
        );
    }

    file.offset =
        31;

    vfs_file_io_test_write_call_count =
        0;

    uint8_t buffer =
        0;

    size_t bytes_written =
        77;

    if (
        vfs_file_write(
            &file,
            &buffer,
            1,
            &bytes_written
        ) != VFS_IO_RESULT_ACCESS_DENIED ||
        bytes_written != 77 ||
        file.offset != 31 ||
        vfs_file_io_test_write_call_count != 0
    ) {
        kernel_panic(
            "VFS write ignored open access mode"
        );
    }

    /*
     * Access validation must precede zero-length handling.
     */
    if (
        vfs_file_write(
            &file,
            NULL,
            0,
            &bytes_written
        ) != VFS_IO_RESULT_ACCESS_DENIED ||
        bytes_written != 77 ||
        file.offset != 31 ||
        vfs_file_io_test_write_call_count != 0
    ) {
        kernel_panic(
            "VFS zero-length write bypassed access validation"
        );
    }

    file.access =
        VFS_OPEN_ACCESS_WRITE;

    if (
        vfs_file_write(
            &file,
            NULL,
            1,
            &bytes_written
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_written != 77 ||
        file.offset != 31 ||
        vfs_file_io_test_write_call_count != 0
    ) {
        kernel_panic(
            "VFS write accepted NULL non-empty buffer"
        );
    }

    if (
        vfs_file_write(
            NULL,
            &buffer,
            1,
            &bytes_written
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_written != 77 ||
        vfs_file_io_test_write_call_count != 0
    ) {
        kernel_panic(
            "VFS write accepted NULL file"
        );
    }

    if (
        vfs_file_write(
            &file,
            &buffer,
            1,
            NULL
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        file.offset != 31 ||
        vfs_file_io_test_write_call_count != 0
    ) {
        kernel_panic(
            "VFS write accepted NULL result storage"
        );
    }

    file.operations =
        NULL;

    if (
        vfs_file_write(
            &file,
            &buffer,
            1,
            &bytes_written
        ) != VFS_IO_RESULT_NOT_SUPPORTED ||
        bytes_written != 77 ||
        file.offset != 31 ||
        vfs_file_io_test_write_call_count != 0
    ) {
        kernel_panic(
            "VFS write accepted missing operation"
        );
    }

    file.operations =
        &operations;

    file.reference_count =
        0;

    if (
        vfs_file_write(
            &file,
            &buffer,
            1,
            &bytes_written
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_written != 77 ||
        file.offset != 31 ||
        vfs_file_io_test_write_call_count != 0
    ) {
        kernel_panic(
            "VFS write accepted dead file"
        );
    }

    file.reference_count =
        1;

    /*
     * A live file cannot validly reference a dead node.
     */
    node.reference_count =
        0;

    if (
        vfs_file_write(
            &file,
            &buffer,
            1,
            &bytes_written
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_written != 77 ||
        file.offset != 31 ||
        vfs_file_io_test_write_call_count != 0
    ) {
        kernel_panic(
            "VFS write accepted file with dead node"
        );
    }

    /*
     * Restore the deliberately corrupted ownership state before teardown.
     * The node owns its original reference plus the one retained by file.
     */
    node.reference_count =
        2;

    if (
        !vfs_file_release(&file) ||
        !vfs_node_release(&node)
    ) {
        kernel_panic(
            "VFS write validation fixture cleanup failed"
        );
    }
}

static void vfs_file_io_test_write_callback_failures(void)
{
    struct vfs_node node;
    struct vfs_file file;

    struct vfs_file_operations operations = {
        .write =
            vfs_file_io_test_write_callback,
    };

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
            VFS_OPEN_ACCESS_WRITE,
            &operations,
            NULL
        )
    ) {
        kernel_panic(
            "VFS write failure fixture initialization failed"
        );
    }

    uint8_t buffer[8] = {0};

    size_t bytes_written =
        73;

    file.offset =
        50;

    vfs_file_io_test_write_call_count =
        0;

    vfs_file_io_test_write_transfer_size =
        5;

    vfs_file_io_test_write_result =
        VFS_IO_RESULT_RESOURCE_EXHAUSTED;

    vfs_file_io_test_write_mutate_offset =
        false;

    if (
        vfs_file_write(
            &file,
            buffer,
            sizeof(buffer),
            &bytes_written
        ) != VFS_IO_RESULT_RESOURCE_EXHAUSTED ||
        bytes_written != 73 ||
        file.offset != 50 ||
        vfs_file_io_test_write_call_count != 1
    ) {
        kernel_panic(
            "VFS write callback failure mutated generic state"
        );
    }

    /*
     * The filesystem callback does not own the shared offset.
     */
    vfs_file_io_test_write_transfer_size =
        1;

    vfs_file_io_test_write_result =
        VFS_IO_RESULT_SUCCESS;

    vfs_file_io_test_write_mutate_offset =
        true;

    if (
        vfs_file_write(
            &file,
            buffer,
            sizeof(buffer),
            &bytes_written
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_written != 73 ||
        file.offset != 50
    ) {
        kernel_panic(
            "VFS write accepted callback offset mutation"
        );
    }

    vfs_file_io_test_write_mutate_offset =
        false;

    /*
     * Successful callbacks cannot claim more bytes than requested.
     */
    vfs_file_io_test_write_transfer_size =
        sizeof(buffer) + 1;

    if (
        vfs_file_write(
            &file,
            buffer,
            sizeof(buffer),
            &bytes_written
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_written != 73 ||
        file.offset != 50
    ) {
        kernel_panic(
            "VFS write accepted oversized transfer"
        );
    }

    /*
     * Generic offset advancement must not wrap.
     */
    file.offset =
        UINT64_MAX - 1;

    vfs_file_io_test_write_transfer_size =
        2;

    if (
        vfs_file_write(
            &file,
            buffer,
            2,
            &bytes_written
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_written != 73 ||
        file.offset != UINT64_MAX - 1
    ) {
        kernel_panic(
            "VFS write accepted offset overflow"
        );
    }

    /*
     * Unknown callback result values cannot escape the generic VFS.
     */
    file.offset =
        50;

    vfs_file_io_test_write_transfer_size =
        1;

    vfs_file_io_test_write_result =
        (enum vfs_io_result) 99;

    if (
        vfs_file_write(
            &file,
            buffer,
            sizeof(buffer),
            &bytes_written
        ) != VFS_IO_RESULT_INVALID_ARGUMENT ||
        bytes_written != 73 ||
        file.offset != 50
    ) {
        kernel_panic(
            "VFS write propagated unknown callback result"
        );
    }

    if (
        !vfs_file_release(&file) ||
        !vfs_node_release(&node)
    ) {
        kernel_panic(
            "VFS write failure fixture cleanup failed"
        );
    }
}

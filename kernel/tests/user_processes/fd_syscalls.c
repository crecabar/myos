// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file fd_syscalls.c
 * @brief Ring-3 descriptor syscall regression fixture.
 *
 * Provides a minimal test-only VFS namespace containing one regular file:
 *
 *   /
 *   └── file
 *
 * The fixture exercises the production syscall, process descriptor and VFS
 * paths without introducing a test filesystem into production kernel code.
 */

#include "fd_syscalls.h"

#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../elf/elf64.h"
#include "../../memory/memory.h"
#include "../../process/create.h"
#include "../../process/namespace.h"
#include "../../process/process.h"
#include "../../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

#define FD_SYSCALLS_TEST_FILE_SIZE 600U

#define FD_SYSCALLS_TEST_INITIAL_BYTE 0xa5U
#define FD_SYSCALLS_TEST_WRITTEN_BYTE 0x5aU

struct fd_syscalls_test_directory {
    struct vfs_node *file;
};

struct fd_syscalls_test_fixture {
    struct vfs_node root;
    struct vfs_node file_node;
    struct vfs_file file_storage;

    struct fd_syscalls_test_directory root_directory;

    uint8_t file_data[FD_SYSCALLS_TEST_FILE_SIZE];
};

extern const uint8_t process_fd_syscalls_fixture_start[];
extern const uint8_t process_fd_syscalls_fixture_end[];

static struct process_instance
    *fd_syscalls_test_instance;

static struct fd_syscalls_test_fixture
    fd_syscalls_fixture;

static uint64_t
    fd_syscalls_test_free_frame_baseline;

// Private helpers declarations
static enum vfs_lookup_result fd_syscalls_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result
);

static enum vfs_open_result fd_syscalls_test_open(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result
);

static enum vfs_stat_result fd_syscalls_test_stat(
    struct vfs_node *node,
    struct vfs_stat *result
);

static enum vfs_io_result fd_syscalls_test_read(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

static enum vfs_io_result fd_syscalls_test_write(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

static void fd_syscalls_test_fixture_initialize(void);

static void fd_syscalls_test_fixture_release(void);

static const struct vfs_node_operations
    fd_syscalls_test_root_operations = {
        .lookup =
            fd_syscalls_test_lookup,
    };

static const struct vfs_node_operations
    fd_syscalls_test_file_operations = {
        .open =
            fd_syscalls_test_open,
        .stat =
            fd_syscalls_test_stat,
    };

static const struct vfs_file_operations
    fd_syscalls_test_io_operations = {
        .read =
            fd_syscalls_test_read,
        .write =
            fd_syscalls_test_write,
    };

// Public functions implementations
void user_process_fd_syscalls_test_prepare(void)
{
    uintptr_t image_start =
        (uintptr_t)
        process_fd_syscalls_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_fd_syscalls_fixture_end;

    if (image_end <= image_start) {
        kernel_panic(
            "FD syscall ELF fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "FD syscall ELF fixture is too large"
        );
    }

    size_t image_size =
        (size_t) image_size_value;

    struct elf64_image image;

    if (!elf64_parse(
        process_fd_syscalls_fixture_start,
        image_size,
        &image
    )) {
        kernel_panic(
            "Unable to parse FD syscall ELF fixture"
        );
    }

    fd_syscalls_test_fixture_initialize();

    /*
     * The VFS fixture uses static kernel storage and therefore consumes no
     * physical user-memory frames. Capture the baseline immediately before
     * creating the process.
     */
    fd_syscalls_test_free_frame_baseline =
        physical_free_frame_count();

    const char *argv[] = {
        "fd-syscalls",
    };

    fd_syscalls_test_instance =
        process_create_elf64(
            &image,
            1,
            argv,
            0,
            NULL
        );

    if (fd_syscalls_test_instance == NULL) {
        kernel_panic(
            "Unable to create FD syscall test process"
        );
    }

    /*
     * The userspace fixture uses absolute paths, so only a namespace root is
     * required. No current working directory is installed for this test.
     */
    if (!process_namespace_root_set(
        fd_syscalls_test_instance,
        &fd_syscalls_fixture.root
    )) {
        kernel_panic(
            "Unable to install FD syscall test namespace root"
        );
    }
}

void user_process_fd_syscalls_test_terminated(
    struct process *process)
{
    if (
        fd_syscalls_test_instance == NULL ||
        process !=
            &fd_syscalls_test_instance->process
    ) {
        kernel_panic(
            "FD syscall test received unexpected process"
        );
    }

    /*
     * The Ring-3 program exits zero only after every syscall result and
     * userspace data verification has matched the published ABI.
     */
    if (
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        kernel_panic(
            "Ring-3 descriptor syscall regression failed"
        );
    }

    if (!process_release_terminated(
        fd_syscalls_test_instance
    )) {
        kernel_panic(
            "Unable to release FD syscall test process"
        );
    }

    fd_syscalls_test_instance =
        NULL;

    /*
     * Process teardown must have released its namespace-root ownership and any
     * descriptor ownership before the fixture's original references are
     * released.
     */
    if (
        fd_syscalls_fixture.root.reference_count != 1 ||
        fd_syscalls_fixture.file_node.reference_count != 1 ||
        fd_syscalls_fixture.file_storage.reference_count != 0
    ) {
        kernel_panic(
            "FD syscall test leaked VFS ownership"
        );
    }

    /*
     * Ring 3 already read the data back through FD_READ. Verify the final
     * backing store independently from the kernel side as well.
     */
    for (
        size_t index = 0;
        index < FD_SYSCALLS_TEST_FILE_SIZE;
        ++index
    ) {
        if (
            fd_syscalls_fixture.file_data[index] !=
            FD_SYSCALLS_TEST_WRITTEN_BYTE
        ) {
            kernel_panic(
                "FD syscall test backing data mismatch"
            );
        }
    }

    fd_syscalls_test_fixture_release();

    if (
        physical_free_frame_count() !=
        fd_syscalls_test_free_frame_baseline
    ) {
        kernel_panic(
            "FD syscall test leaked physical frames"
        );
    }

    diagnostics_write(
        "[syscall] Ring-3 descriptor syscall regression passed\n"
    );
}

// Private helpers implementations
static enum vfs_lookup_result fd_syscalls_test_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result)
{
    if (
        directory !=
            &fd_syscalls_fixture.root ||
        name == NULL ||
        result == NULL
    ) {
        kernel_panic(
            "FD syscall lookup received invalid input"
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
            fd_syscalls_fixture
                .root_directory
                .file;

        return
            VFS_LOOKUP_RESULT_FOUND;
    }

    return
        VFS_LOOKUP_RESULT_NOT_FOUND;
}

static enum vfs_open_result fd_syscalls_test_open(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result)
{
    if (
        node !=
            &fd_syscalls_fixture.file_node ||
        result == NULL
    ) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    /*
     * The regression never keeps two simultaneous opens of /file, so one
     * reusable open-file-description storage object is sufficient.
     */
    if (
        fd_syscalls_fixture
            .file_storage
            .reference_count != 0
    ) {
        return
            VFS_OPEN_RESULT_RESOURCE_EXHAUSTED;
    }

    if (!vfs_file_initialize(
        &fd_syscalls_fixture.file_storage,
        node,
        access,
        &fd_syscalls_test_io_operations,
        &fd_syscalls_fixture
    )) {
        return
            VFS_OPEN_RESULT_RESOURCE_EXHAUSTED;
    }

    *result =
        &fd_syscalls_fixture.file_storage;

    return
        VFS_OPEN_RESULT_OPENED;
}

static enum vfs_stat_result fd_syscalls_test_stat(
    struct vfs_node *node,
    struct vfs_stat *result)
{
    if (
        node !=
            &fd_syscalls_fixture.file_node ||
        result == NULL
    ) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    result->type =
        VFS_NODE_TYPE_REGULAR_FILE;

    result->size =
        FD_SYSCALLS_TEST_FILE_SIZE;

    return
        VFS_STAT_RESULT_SUCCESS;
}

static enum vfs_io_result fd_syscalls_test_read(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    if (
        file !=
            &fd_syscalls_fixture.file_storage ||
        file->private_data !=
            &fd_syscalls_fixture ||
        buffer == NULL ||
        size == 0 ||
        bytes_read == NULL
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Reading at or beyond EOF is a successful zero-byte transfer.
     */
    if (
        offset >=
        FD_SYSCALLS_TEST_FILE_SIZE
    ) {
        *bytes_read =
            0;

        return
            VFS_IO_RESULT_SUCCESS;
    }

    size_t start =
        (size_t) offset;

    size_t available =
        FD_SYSCALLS_TEST_FILE_SIZE -
        start;

    size_t transfer_size =
        size < available
            ? size
            : available;

    uint8_t *destination =
        buffer;

    for (
        size_t index = 0;
        index < transfer_size;
        ++index
    ) {
        destination[index] =
            fd_syscalls_fixture
                .file_data[start + index];
    }

    *bytes_read =
        transfer_size;

    return
        VFS_IO_RESULT_SUCCESS;
}

static enum vfs_io_result fd_syscalls_test_write(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    if (
        file !=
            &fd_syscalls_fixture.file_storage ||
        file->private_data !=
            &fd_syscalls_fixture ||
        buffer == NULL ||
        size == 0 ||
        bytes_written == NULL
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    /*
     * This fixture models a fixed-size regular file. A write beginning at or
     * beyond its end succeeds with zero transferred bytes.
     */
    if (
        offset >=
        FD_SYSCALLS_TEST_FILE_SIZE
    ) {
        *bytes_written =
            0;

        return
            VFS_IO_RESULT_SUCCESS;
    }

    size_t start =
        (size_t) offset;

    size_t available =
        FD_SYSCALLS_TEST_FILE_SIZE -
        start;

    size_t transfer_size =
        size < available
            ? size
            : available;

    const uint8_t *source =
        buffer;

    for (
        size_t index = 0;
        index < transfer_size;
        ++index
    ) {
        fd_syscalls_fixture
            .file_data[start + index] =
                source[index];
    }

    *bytes_written =
        transfer_size;

    return
        VFS_IO_RESULT_SUCCESS;
}

static void fd_syscalls_test_fixture_initialize(void)
{
    fd_syscalls_fixture =
        (struct fd_syscalls_test_fixture) {0};

    fd_syscalls_fixture
        .root_directory
        .file =
            &fd_syscalls_fixture.file_node;

    for (
        size_t index = 0;
        index < FD_SYSCALLS_TEST_FILE_SIZE;
        ++index
    ) {
        fd_syscalls_fixture.file_data[index] =
            FD_SYSCALLS_TEST_INITIAL_BYTE;
    }

    if (
        !vfs_node_initialize(
            &fd_syscalls_fixture.root,
            VFS_NODE_TYPE_DIRECTORY,
            &fd_syscalls_test_root_operations,
            &fd_syscalls_fixture.root_directory
        ) ||
        !vfs_node_initialize(
            &fd_syscalls_fixture.file_node,
            VFS_NODE_TYPE_REGULAR_FILE,
            &fd_syscalls_test_file_operations,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize FD syscall VFS fixture"
        );
    }
}

static void fd_syscalls_test_fixture_release(void)
{
    if (
        !vfs_node_release(
            &fd_syscalls_fixture.file_node
        ) ||
        !vfs_node_release(
            &fd_syscalls_fixture.root
        )
    ) {
        kernel_panic(
            "Unable to release FD syscall VFS fixture"
        );
    }
}

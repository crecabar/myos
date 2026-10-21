// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file blocking_read.c
 * @brief End-to-end Ring-3 blocking read regression.
 */

#include "blocking_read.h"

#include <myos/kernel/core/panic.h>
#include <myos/kernel/core/wait_queue.h>
#include <myos/kernel/diagnostics/diagnostics.h>
#include <myos/kernel/memory/memory.h>
#include <myos/kernel/process/create.h>
#include <myos/kernel/process/executable.h>
#include <myos/kernel/process/fd_table.h>
#include <myos/kernel/process/instance.h>
#include <myos/kernel/process/process.h>
#include <myos/kernel/scheduler/scheduler.h>
#include <myos/kernel/vfs/root.h>
#include <myos/kernel/vfs/vfs.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLOCKING_READ_TEST_BYTE 0x4dU

struct blocking_read_test_fixture {
    struct vfs_node node;
    struct vfs_file reader_file;
    struct vfs_file writer_file;

    struct wait_queue queue;

    struct process_instance *reader;
    struct process_instance *writer;

    uint64_t reader_pid;
    uint64_t writer_pid;

    uint64_t free_frames_before;
    size_t scheduler_count_before;

    size_t read_calls;
    size_t readiness_calls;
    size_t registration_calls;
    size_t write_calls;
    size_t notification_count;

    bool available;
    bool reader_completed;
    bool writer_completed;

    uint8_t byte;
};

static struct blocking_read_test_fixture
    blocking_read_fixture;

static enum vfs_io_result blocking_read_test_read(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

static enum vfs_readiness_result blocking_read_test_ready(
    struct vfs_file *file,
    bool *ready
);

static enum vfs_readiness_result blocking_read_test_register(
    struct vfs_file *file,
    struct wait_queue_entry *entry,
    wait_queue_notification_fn notification,
    void *context
);

static enum vfs_io_result blocking_read_test_write(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

static const struct vfs_file_operations
    blocking_read_test_operations = {
        .read = blocking_read_test_read,
        .read_ready = blocking_read_test_ready,
        .read_wait_register = blocking_read_test_register,
        .write = blocking_read_test_write,
    };

static enum vfs_io_result blocking_read_test_read(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    struct blocking_read_test_fixture *fixture =
        &blocking_read_fixture;

    if (
        file != &fixture->reader_file ||
        file->private_data != fixture ||
        offset != 0U ||
        buffer == NULL ||
        size == 0U ||
        bytes_read == NULL
    ) {
        return VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    ++fixture->read_calls;

    if (!fixture->available) {
        return VFS_IO_RESULT_WOULD_BLOCK;
    }

    /*
     * The writer published exactly one byte.
     * The generic VFS owns offset advancement.
     */
    uint8_t *destination = buffer;

    destination[0] = fixture->byte;

    *bytes_read = 1U;

    fixture->available = false;

    return VFS_IO_RESULT_SUCCESS;
}

static enum vfs_readiness_result blocking_read_test_ready(
    struct vfs_file *file,
    bool *ready)
{
    struct blocking_read_test_fixture *fixture =
        &blocking_read_fixture;

    if (
        file != &fixture->reader_file ||
        file->private_data != fixture ||
        ready == NULL
    ) {
        return VFS_READINESS_RESULT_INVALID_ARGUMENT;
    }

    ++fixture->readiness_calls;

    *ready = fixture->available;

    return VFS_READINESS_RESULT_SUCCESS;
}

static enum vfs_readiness_result blocking_read_test_register(
    struct vfs_file *file,
    struct wait_queue_entry *entry,
    wait_queue_notification_fn notification,
    void *context)
{
    struct blocking_read_test_fixture *fixture =
        &blocking_read_fixture;

    if (
        file != &fixture->reader_file ||
        file->private_data != fixture ||
        entry == NULL ||
        notification == NULL
    ) {
        return VFS_READINESS_RESULT_INVALID_ARGUMENT;
    }

    if (!wait_queue_register(
        &fixture->queue,
        entry,
        notification,
        context
    )) {
        return VFS_READINESS_RESULT_INVALID_ARGUMENT;
    }

    ++fixture->registration_calls;

    return VFS_READINESS_RESULT_SUCCESS;
}

static enum vfs_io_result blocking_read_test_write(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    struct blocking_read_test_fixture *fixture =
        &blocking_read_fixture;

    if (
        file != &fixture->writer_file ||
        file->private_data != fixture ||
        offset != 0U ||
        buffer == NULL ||
        size != 1U ||
        bytes_written == NULL
    ) {
        return VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    /*
     * The reader must already have entered BLOCKED.
     * Otherwise this test has not exercised the
     * blocking syscall path.
     */
    if (
        fixture->reader == NULL ||
        fixture->writer == NULL ||
        scheduler_current() !=
            &fixture->writer->process ||
        fixture->reader->process.state !=
            PROCESS_STATE_BLOCKED ||
        !fixture->reader->read_wait_active ||
        fixture->reader->read_wait_notified ||
        !wait_queue_entry_registered(
            &fixture->reader->read_wait.entry
        ) ||
        fixture->available ||
        fixture->write_calls != 0U
    ) {
        kernel_panic(
            "Writer did not observe BLOCKED reader"
        );
    }

    const uint8_t *source = buffer;

    if (source[0] != BLOCKING_READ_TEST_BYTE) {
        kernel_panic(
            "Blocking read writer published unexpected byte"
        );
    }

    /*
     * Publish data before notifying readers.
     *
     * The syscall interrupt gate serializes this
     * operation with scheduler transitions on BSP.
     */
    fixture->byte = source[0];
    fixture->available = true;

    ++fixture->write_calls;

    fixture->notification_count =
        wait_queue_notify_all(
            &fixture->queue
        );

    if (
        fixture->notification_count != 1U ||
        fixture->reader->process.state !=
            PROCESS_STATE_READY ||
        !fixture->reader->read_wait_active ||
        !fixture->reader->read_wait_notified ||
        fixture->reader->read_wait.file != NULL ||
        wait_queue_entry_registered(
            &fixture->reader->read_wait.entry
        ) ||
        fixture->queue.head != NULL ||
        fixture->queue.tail != NULL ||
        fixture->reader_file.reference_count != 2U
    ) {
        kernel_panic(
            "Writer failed to wake blocked reader"
        );
    }

    *bytes_written = 1U;

    return VFS_IO_RESULT_SUCCESS;
}

void user_process_blocking_read_test_prepare(
    const struct vfs_mount_table *mounts)
{
    if (mounts == NULL) {
        kernel_panic(
            "Blocking read test received NULL mounts"
        );
    }

    struct blocking_read_test_fixture *fixture =
        &blocking_read_fixture;

    *fixture = (struct blocking_read_test_fixture) {0};

    wait_queue_init(&fixture->queue);

    if (
        !vfs_node_initialize(
            &fixture->node,
            VFS_NODE_TYPE_CHARACTER_DEVICE,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &fixture->reader_file,
            &fixture->node,
            VFS_OPEN_ACCESS_READ,
            &blocking_read_test_operations,
            fixture
        ) ||
        !vfs_file_initialize(
            &fixture->writer_file,
            &fixture->node,
            VFS_OPEN_ACCESS_WRITE,
            &blocking_read_test_operations,
            fixture
        )
    ) {
        kernel_panic(
            "Unable to initialize blocking read VFS fixture"
        );
    }

    fixture->free_frames_before =
        physical_free_frame_count();

    fixture->scheduler_count_before =
        scheduler_test_process_count();

    struct vfs_node *root = vfs_root_get();

    if (root == NULL) {
        kernel_panic(
            "Blocking read test has no system root"
        );
    }

    static const char executable_path[] =
        "/bin/blocking-read";

    struct process_executable executable = {0};

    if (
        process_executable_open_elf64_from_namespace(
            root,
            mounts,
            executable_path,
            sizeof(executable_path) - 1U,
            &executable
        ) != PROCESS_EXECUTABLE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to open blocking read test executable"
        );
    }

    const char *reader_argv[] = {
        executable_path,
        "r",
    };

    fixture->reader =
        process_create_elf64(
            &executable.image,
            2U,
            reader_argv,
            0U,
            NULL
        );

    if (fixture->reader == NULL) {
        kernel_panic(
            "Unable to create blocking read reader"
        );
    }

    const char *writer_argv[] = {
        executable_path,
        "w",
    };

    fixture->writer =
        process_create_elf64(
            &executable.image,
            2U,
            writer_argv,
            0U,
            NULL
        );

    if (fixture->writer == NULL) {
        kernel_panic(
            "Unable to create blocking read writer"
        );
    }

    fixture->reader_pid = fixture->reader->process.id;
    fixture->writer_pid = fixture->writer->process.id;

    if (!process_executable_close(&executable)) {
        kernel_panic(
            "Unable to close blocking read test executable"
        );
    }

    /*
     * Processes without initial VFS provisioning have
     * empty descriptor tables. Install the two ends
     * explicitly before either process runs.
     */
    if (
        !process_fd_table_install_at(
            &fixture->reader->file_descriptors,
            0U,
            &fixture->reader_file
        ) ||
        !process_fd_table_install_at(
            &fixture->writer->file_descriptors,
            1U,
            &fixture->writer_file
        )
    ) {
        kernel_panic(
            "Unable to install blocking read descriptors"
        );
    }

    if (
        fixture->reader->process.state !=
            PROCESS_STATE_READY ||
        fixture->writer->process.state !=
            PROCESS_STATE_READY ||
        fixture->reader_file.reference_count != 2U ||
        fixture->writer_file.reference_count != 2U ||
        fixture->node.reference_count != 3U ||
        scheduler_test_process_count() !=
            fixture->scheduler_count_before + 2U
    ) {
        kernel_panic(
            "Blocking read test preparation is inconsistent"
        );
    }

    diagnostics_printf(
        "[syscall] Blocking read test prepared: "
        "reader=%u writer=%u\n",
        fixture->reader_pid,
        fixture->writer_pid
    );
}

bool user_process_blocking_read_test_terminated(
    struct process *process)
{
    struct blocking_read_test_fixture *fixture =
        &blocking_read_fixture;

    if (process == NULL) {
        kernel_panic(
            "Blocking read test received NULL process"
        );
    }

    if (
        fixture->writer != NULL &&
        process == &fixture->writer->process
    ) {
        if (
            fixture->writer_completed ||
            process->id != fixture->writer_pid ||
            process->state !=
                PROCESS_STATE_TERMINATED ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status != 0U ||
            fixture->write_calls != 1U ||
            fixture->notification_count != 1U ||
            fixture->writer_file.offset != 1U ||
            fixture->writer_file.reference_count != 1U
        ) {
            kernel_panic(
                "Blocking read writer termination failed"
            );
        }

        /*
         * The reader may have terminated already if
         * the writer was preempted after notification.
         *
         * Otherwise it must remain READY with its
         * delivered logical wait pending.
         */
        if (fixture->reader_completed) {
            if (
                fixture->reader != NULL ||
                fixture->reader_file.reference_count != 1U
            ) {
                kernel_panic(
                    "Writer observed inconsistent completed reader"
                );
            }
        } else {
            if (
                fixture->reader == NULL ||
                fixture->reader->process.state !=
                    PROCESS_STATE_READY ||
                !fixture->reader->read_wait_active ||
                !fixture->reader->read_wait_notified
            ) {
                kernel_panic(
                    "Writer observed invalid reader wakeup state"
                );
            }
        }

        if (!process_release_terminated(
            fixture->writer
        )) {
            kernel_panic(
                "Unable to release blocking read writer"
            );
        }

        fixture->writer = NULL;
        fixture->writer_completed = true;
    } else if (
        fixture->reader != NULL &&
        process == &fixture->reader->process
    ) {
        /*
         * The reader must have resumed after the
         * notification and consumed the published byte.
         *
         * Its completion does not depend on whether
         * the writer has already exited.
         */
        if (
            fixture->reader_completed ||
            process->id != fixture->reader_pid ||
            process->state !=
                PROCESS_STATE_TERMINATED ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status != 0U ||
            fixture->read_calls != 2U ||
            fixture->readiness_calls != 1U ||
            fixture->registration_calls != 1U ||
            fixture->write_calls != 1U ||
            fixture->notification_count != 1U ||
            fixture->available ||
            fixture->reader->read_wait_active ||
            fixture->reader->read_wait_notified ||
            fixture->reader->read_wait.file != NULL ||
            wait_queue_entry_registered(
                &fixture->reader->read_wait.entry
            ) ||
            fixture->queue.head != NULL ||
            fixture->queue.tail != NULL ||
            fixture->reader_file.offset != 1U ||
            fixture->reader_file.reference_count != 1U ||
            fixture->node.reference_count != 3U
        ) {
            kernel_panic(
                "Blocking read reader completion failed"
            );
        }

        if (!process_release_terminated(
            fixture->reader
        )) {
            kernel_panic(
                "Unable to release blocking read reader"
            );
        }

        fixture->reader = NULL;
        fixture->reader_completed = true;
    } else {
        kernel_panic(
            "Blocking read test received unexpected process"
        );
    }

    /*
     * Either process may terminate first.
     *
     * Do not release shared VFS storage until both
     * processes have completed their lifecycles.
     */
    if (
        !fixture->reader_completed ||
        !fixture->writer_completed
    ) {
        return false;
    }

    if (
        fixture->reader != NULL ||
        fixture->writer != NULL ||
        fixture->read_calls != 2U ||
        fixture->readiness_calls != 1U ||
        fixture->registration_calls != 1U ||
        fixture->write_calls != 1U ||
        fixture->notification_count != 1U ||
        fixture->available ||
        fixture->queue.head != NULL ||
        fixture->queue.tail != NULL ||
        fixture->reader_file.offset != 1U ||
        fixture->writer_file.offset != 1U ||
        fixture->reader_file.reference_count != 1U ||
        fixture->writer_file.reference_count != 1U ||
        fixture->node.reference_count != 3U
    ) {
        kernel_panic(
            "Blocking read final fixture state is inconsistent"
        );
    }

    /*
     * Both descriptor owners have terminated.
     * Release the remaining test-owned references.
     */
    if (
        !vfs_file_release(
            &fixture->reader_file
        ) ||
        !vfs_file_release(
            &fixture->writer_file
        ) ||
        fixture->node.reference_count != 1U ||
        !vfs_node_release(
            &fixture->node
        )
    ) {
        kernel_panic(
            "Blocking read VFS cleanup failed"
        );
    }

    if (
        fixture->reader_file.reference_count != 0U ||
        fixture->writer_file.reference_count != 0U ||
        fixture->node.reference_count != 0U ||
        scheduler_test_process_count() !=
            fixture->scheduler_count_before ||
        physical_free_frame_count() !=
            fixture->free_frames_before
    ) {
        kernel_panic(
            "Blocking read regression leaked resources"
        );
    }

    diagnostics_printf(
        "[syscall] Ring-3 blocking read regression passed: "
        "reader=%u writer=%u\n",
        fixture->reader_pid,
        fixture->writer_pid
    );

    return true;
}

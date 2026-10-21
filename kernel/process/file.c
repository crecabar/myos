// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file file.c
 * @brief Per-process descriptor-level file operations implementation.
 */

#include <myos/kernel/process/file.h>

#include <myos/kernel/process/fd_table.h>
#include <myos/kernel/process/path.h>

#include <myos/kernel/core/panic.h>
#include "../scheduler/scheduler.h"

#include <stddef.h>

// Private functions and helpers declarations
static enum process_file_result process_file_map_path_result(
    enum process_path_result result
);

static enum process_file_result process_file_map_open_result(
    enum vfs_open_result result
);

static enum process_file_result process_file_map_io_result(
    enum vfs_io_result result
);

static enum process_file_result process_file_map_seek_result(
    enum vfs_seek_result result
);

static enum process_file_result process_file_map_stat_result(
    enum vfs_stat_result result
);

static enum process_file_result process_file_map_readiness_result(
    enum vfs_readiness_result result
);

static void process_file_read_wait_notification(
    void *context
);

// Public functions implementations
enum process_file_result process_file_open(
    struct process_instance *instance,
    const char *path,
    size_t path_length,
    enum vfs_open_access access,
    size_t *descriptor)
{
    if (
        instance == NULL ||
        descriptor == NULL
    ) {
        return
            PROCESS_FILE_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_node *node =
        NULL;

    enum process_path_result path_result =
        process_path_resolve(
            instance,
            path,
            path_length,
            &node
        );

    if (
        path_result !=
        PROCESS_PATH_RESULT_RESOLVED
    ) {
        return
            process_file_map_path_result(
                path_result
            );
    }

    /*
     * Resolution returned exactly one owned node reference.
     */
    struct vfs_file *file =
        NULL;

    enum vfs_open_result open_result =
        vfs_node_open(
            node,
            access,
            &file
        );

    /*
     * A successful open-file description independently owns its node
     * reference, so the resolver-owned reference is no longer needed.
     *
     * Failure here would indicate an internal VFS ownership violation.
     */
    if (!vfs_node_release(
        node
    )) {
        kernel_panic(
            "Process file open failed to release resolved node"
        );
    }

    if (
        open_result !=
        VFS_OPEN_RESULT_OPENED
    ) {
        return
            process_file_map_open_result(
                open_result
            );
    }

    /*
     * vfs_node_open() transferred exactly one owned vfs_file reference to
     * this operation. Installation retains an independent descriptor-owned
     * reference.
     */
    size_t installed_descriptor;

    if (!process_fd_table_install(
        &instance->file_descriptors,
        file,
        &installed_descriptor
    )) {
        if (!vfs_file_release(
            file
        )) {
            kernel_panic(
                "Process file open failed to roll back open file"
            );
        }

        return
            PROCESS_FILE_RESULT_RESOURCE_EXHAUSTED;
    }

    /*
     * Drop the reference transferred by vfs_node_open(). The descriptor table
     * now owns the surviving reference.
     *
     * Since install() successfully retained a live file, this release cannot
     * legitimately fail.
     */
    if (!vfs_file_release(
        file
    )) {
        kernel_panic(
            "Process file open failed to transfer descriptor ownership"
        );
    }

    *descriptor =
        installed_descriptor;

    return
        PROCESS_FILE_RESULT_SUCCESS;
}

enum process_file_result process_file_close(
    struct process_instance *instance,
    size_t descriptor)
{
    if (instance == NULL) {
        return
            PROCESS_FILE_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Distinguish a userspace-visible bad descriptor from an internal failure
     * while releasing an already validated table entry.
     */
    if (
        process_fd_table_get(
            &instance->file_descriptors,
            descriptor
        ) == NULL
    ) {
        return
            PROCESS_FILE_RESULT_BAD_DESCRIPTOR;
    }

    if (!process_fd_table_close(
        &instance->file_descriptors,
        descriptor
    )) {
        kernel_panic(
            "Process file close failed for live descriptor"
        );
    }

    return
        PROCESS_FILE_RESULT_SUCCESS;
}

enum process_file_result process_file_read(
    struct process_instance *instance,
    size_t descriptor,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    if (
        instance == NULL ||
        bytes_read == NULL
    ) {
        return
            PROCESS_FILE_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_file *file =
        process_fd_table_get(
            &instance->file_descriptors,
            descriptor
        );

    if (file == NULL) {
        return
            PROCESS_FILE_RESULT_BAD_DESCRIPTOR;
    }

    size_t transferred;

    enum vfs_io_result io_result =
        vfs_file_read(
            file,
            buffer,
            size,
            &transferred
        );

    if (io_result != VFS_IO_RESULT_SUCCESS) {
        return
            process_file_map_io_result(
                io_result
            );
    }

    *bytes_read =
        transferred;

    return
        PROCESS_FILE_RESULT_SUCCESS;
}

enum process_file_result process_file_read_ready(
    struct process_instance *instance,
    size_t descriptor,
    bool *ready)
{
    if (
        instance == NULL ||
        ready == NULL
    ) {
        return PROCESS_FILE_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_file *file =
        process_fd_table_get(
            &instance->file_descriptors,
            descriptor
        );

    if (file == NULL) {
        return PROCESS_FILE_RESULT_BAD_DESCRIPTOR;
    }

    bool available = false;

    enum vfs_readiness_result result =
        vfs_file_read_ready(
            file,
            &available
        );

    if (result != VFS_READINESS_RESULT_SUCCESS) {
        return process_file_map_readiness_result(
            result
        );
    }

    *ready = available;

    return PROCESS_FILE_RESULT_SUCCESS;
}

enum process_file_result process_file_read_wait_register(
    struct process_instance *instance,
    size_t descriptor)
{
    if (
        instance == NULL ||
        instance->process.instance != instance ||
        instance->process.state ==
            PROCESS_STATE_TERMINATED ||
        instance->wait_active ||
        instance->read_wait_active ||
        instance->read_wait_notified ||
        instance->read_wait.file != NULL ||
        wait_queue_entry_registered(
            &instance->read_wait.entry
        )
    ) {
        return PROCESS_FILE_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_file *file =
        process_fd_table_get(
            &instance->file_descriptors,
            descriptor
        );

    if (file == NULL) {
        return PROCESS_FILE_RESULT_BAD_DESCRIPTOR;
    }

    /*
     * The VFS registration retains the underlying
     * open-file description.
     *
     * Backend registration must not invoke the callback
     * synchronously. Event delivery must be serialized
     * against this operation.
     */
    enum vfs_readiness_result result =
        vfs_file_read_wait_register(
            file,
            &instance->read_wait,
            process_file_read_wait_notification,
            instance
        );

    if (result != VFS_READINESS_RESULT_SUCCESS) {
        return process_file_map_readiness_result(result);
    }

    instance->read_wait_notified = false;
    instance->read_wait_active = true;

    return PROCESS_FILE_RESULT_SUCCESS;
}

enum process_file_result process_file_read_wait_prepare(
    struct process_instance *instance,
    size_t descriptor,
    bool *should_block)
{
    if (should_block == NULL) {
        return PROCESS_FILE_RESULT_INVALID_ARGUMENT;
    }

    enum process_file_result result =
        process_file_read_wait_register(
            instance,
            descriptor
        );

    if (result != PROCESS_FILE_RESULT_SUCCESS) {
        return result;
    }

    /*
     * The registration is now active.
     *
     * Recheck readability after registration so an
     * event published before the registration cannot
     * leave the process waiting indefinitely.
     */
    bool ready = false;

    result = process_file_read_ready(
        instance,
        descriptor,
        &ready
    );

    if (
        result != PROCESS_FILE_RESULT_SUCCESS ||
        ready ||
        instance->read_wait_notified
    ) {
        if (!process_file_read_wait_cancel(
            instance
        )) {
            kernel_panic(
                "Unable to cancel prepared read wait"
            );
        }

        if (result != PROCESS_FILE_RESULT_SUCCESS) {
            return result;
        }

        *should_block = false;

        return PROCESS_FILE_RESULT_SUCCESS;
    }

    /*
     * The caller may now block while preserving the
     * registration. This decision remains valid only
     * within the same event-serialized critical section.
     */
    *should_block = true;

    return PROCESS_FILE_RESULT_SUCCESS;
}

bool process_file_read_wait_cancel(
    struct process_instance *instance)
{
    if (
        instance == NULL ||
        instance->process.instance != instance ||
        !instance->read_wait_active
    ) {
        return false;
    }

    /*
     * Delivery detaches the VFS registration before
     * notifying this layer.
     *
     * A delivered request therefore has no remaining
     * file reference to cancel.
     */
    if (instance->read_wait_notified) {
        if (
            instance->read_wait.file != NULL ||
            wait_queue_entry_registered(
                &instance->read_wait.entry
            )
        ) {
            kernel_panic(
                "Delivered process read wait retained VFS state"
            );
        }
    } else {
        if (!vfs_read_wait_cancel(
            &instance->read_wait
        )) {
            return false;
        }
    }

    instance->read_wait_active = false;
    instance->read_wait_notified = false;

    return true;
}

static void process_file_read_wait_notification(
    void *context)
{
    struct process_instance *instance = context;

    if (
        instance == NULL ||
        instance->process.instance != instance ||
        !instance->read_wait_active ||
        instance->read_wait_notified ||
        instance->read_wait.file != NULL ||
        wait_queue_entry_registered(
            &instance->read_wait.entry
        )
    ) {
        kernel_panic(
            "Process read wait received invalid notification"
        );
    }

    /*
     * Delivery has already detached the VFS registration.
     *
     * Preserve the logical request until its owner
     * explicitly clears it.
     */
    instance->read_wait_notified = true;

    /*
     * Notification may arrive before the process blocks.
     * In that case the recorded event is sufficient.
     *
     * Once BLOCKED, the scheduler owns the transition
     * back to READY.
     */
    if (
        instance->process.state ==
            PROCESS_STATE_BLOCKED
    ) {
        if (!scheduler_wake_blocked(
            &instance->process
        )) {
            kernel_panic(
                "Unable to wake process waiting for read"
            );
        }
    }
}

enum process_file_result process_file_write(
    struct process_instance *instance,
    size_t descriptor,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    if (
        instance == NULL ||
        bytes_written == NULL
    ) {
        return
            PROCESS_FILE_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_file *file =
        process_fd_table_get(
            &instance->file_descriptors,
            descriptor
        );

    if (file == NULL) {
        return
            PROCESS_FILE_RESULT_BAD_DESCRIPTOR;
    }

    size_t transferred;

    enum vfs_io_result io_result =
        vfs_file_write(
            file,
            buffer,
            size,
            &transferred
        );

    if (io_result != VFS_IO_RESULT_SUCCESS) {
        return
            process_file_map_io_result(
                io_result
            );
    }

    *bytes_written =
        transferred;

    return
        PROCESS_FILE_RESULT_SUCCESS;
}

enum process_file_result process_file_seek(
    struct process_instance *instance,
    size_t descriptor,
    int64_t offset,
    enum vfs_seek_origin origin,
    uint64_t *result)
{
    return process_file_seek_bounded(
        instance,
        descriptor,
        offset,
        origin,
        UINT64_MAX,
        result
    );
}

enum process_file_result process_file_seek_bounded(
    struct process_instance *instance,
    size_t descriptor,
    int64_t offset,
    enum vfs_seek_origin origin,
    uint64_t maximum_offset,
    uint64_t *result)
{
    if (
        instance == NULL ||
        result == NULL
    ) {
        return
            PROCESS_FILE_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_file *file =
        process_fd_table_get(
            &instance->file_descriptors,
            descriptor
        );

    if (file == NULL) {
        return
            PROCESS_FILE_RESULT_BAD_DESCRIPTOR;
    }

    uint64_t original_offset =
        file->offset;

    uint64_t new_offset;

    enum vfs_seek_result seek_result =
        vfs_file_seek(
            file,
            offset,
            origin,
            &new_offset
        );

    if (
        seek_result !=
        VFS_SEEK_RESULT_SUCCESS
    ) {
        return
            process_file_map_seek_result(
                seek_result
            );
    }

    if (new_offset > maximum_offset) {
        /*
         * vfs_file_seek() mutates only the generic open-file offset after all
         * validation has succeeded. Under the current serialized VFS model,
         * restoring it here makes the bounded operation transactional.
         */
        file->offset =
            original_offset;

        return
            PROCESS_FILE_RESULT_OVERFLOW;
    }

    *result =
        new_offset;

    return
        PROCESS_FILE_RESULT_SUCCESS;
}

enum process_file_result process_file_stat(
    struct process_instance *instance,
    size_t descriptor,
    struct vfs_stat *result)
{
    if (
        instance == NULL ||
        result == NULL
    ) {
        return
            PROCESS_FILE_RESULT_INVALID_ARGUMENT;
    }

    struct vfs_file *file =
        process_fd_table_get(
            &instance->file_descriptors,
            descriptor
        );

    if (file == NULL) {
        return
            PROCESS_FILE_RESULT_BAD_DESCRIPTOR;
    }

    struct vfs_stat metadata;

    enum vfs_stat_result stat_result =
        vfs_node_stat(
            file->node,
            &metadata
        );

    if (
        stat_result !=
        VFS_STAT_RESULT_SUCCESS
    ) {
        return
            process_file_map_stat_result(
                stat_result
            );
    }

    *result =
        metadata;

    return
        PROCESS_FILE_RESULT_SUCCESS;
}

// Private functions and helpers implementations
static enum process_file_result process_file_map_path_result(
    enum process_path_result result)
{
    switch (result) {
        case PROCESS_PATH_RESULT_RESOLVED:
            return
                PROCESS_FILE_RESULT_SUCCESS;

        case PROCESS_PATH_RESULT_NOT_FOUND:
            return
                PROCESS_FILE_RESULT_NOT_FOUND;

        case PROCESS_PATH_RESULT_INVALID_ARGUMENT:
            return
                PROCESS_FILE_RESULT_INVALID_ARGUMENT;

        case PROCESS_PATH_RESULT_NO_NAMESPACE_ROOT:
            return
                PROCESS_FILE_RESULT_NO_NAMESPACE_ROOT;

        case PROCESS_PATH_RESULT_NO_CURRENT_DIRECTORY:
            return
                PROCESS_FILE_RESULT_NO_CURRENT_DIRECTORY;

        case PROCESS_PATH_RESULT_NOT_DIRECTORY:
            return
                PROCESS_FILE_RESULT_NOT_DIRECTORY;

        case PROCESS_PATH_RESULT_NOT_SUPPORTED:
            return
                PROCESS_FILE_RESULT_NOT_SUPPORTED;

        case PROCESS_PATH_RESULT_RESOURCE_EXHAUSTED:
            return
                PROCESS_FILE_RESULT_RESOURCE_EXHAUSTED;
    }

    return
        PROCESS_FILE_RESULT_INVALID_ARGUMENT;
}

static enum process_file_result process_file_map_open_result(
    enum vfs_open_result result)
{
    switch (result) {
        case VFS_OPEN_RESULT_OPENED:
            return
                PROCESS_FILE_RESULT_SUCCESS;

        case VFS_OPEN_RESULT_INVALID_ARGUMENT:
            return
                PROCESS_FILE_RESULT_INVALID_ARGUMENT;

        case VFS_OPEN_RESULT_NOT_SUPPORTED:
            return
                PROCESS_FILE_RESULT_NOT_SUPPORTED;

        case VFS_OPEN_RESULT_ACCESS_DENIED:
            return
                PROCESS_FILE_RESULT_ACCESS_DENIED;

        case VFS_OPEN_RESULT_RESOURCE_EXHAUSTED:
            return
                PROCESS_FILE_RESULT_RESOURCE_EXHAUSTED;
    }

    return
        PROCESS_FILE_RESULT_INVALID_ARGUMENT;
}

static enum process_file_result process_file_map_io_result(
    enum vfs_io_result result)
{
    switch (result) {
        case VFS_IO_RESULT_SUCCESS:
            return
                PROCESS_FILE_RESULT_SUCCESS;

        case VFS_IO_RESULT_INVALID_ARGUMENT:
            return
                PROCESS_FILE_RESULT_INVALID_ARGUMENT;

        case VFS_IO_RESULT_NOT_SUPPORTED:
            return
                PROCESS_FILE_RESULT_NOT_SUPPORTED;

        case VFS_IO_RESULT_ACCESS_DENIED:
            return
                PROCESS_FILE_RESULT_ACCESS_DENIED;

        case VFS_IO_RESULT_RESOURCE_EXHAUSTED:
            return
                PROCESS_FILE_RESULT_RESOURCE_EXHAUSTED;
        case VFS_IO_RESULT_WOULD_BLOCK:
            return PROCESS_FILE_RESULT_WOULD_BLOCK;
    }

    return
        PROCESS_FILE_RESULT_INVALID_ARGUMENT;
}

static enum process_file_result process_file_map_seek_result(
    enum vfs_seek_result result)
{
    switch (result) {
        case VFS_SEEK_RESULT_SUCCESS:
            return
                PROCESS_FILE_RESULT_SUCCESS;

        case VFS_SEEK_RESULT_INVALID_ARGUMENT:
            return
                PROCESS_FILE_RESULT_INVALID_ARGUMENT;

        case VFS_SEEK_RESULT_NOT_SUPPORTED:
            return
                PROCESS_FILE_RESULT_NOT_SUPPORTED;
    }

    return
        PROCESS_FILE_RESULT_INVALID_ARGUMENT;
}

static enum process_file_result process_file_map_stat_result(
    enum vfs_stat_result result)
{
    switch (result) {
        case VFS_STAT_RESULT_SUCCESS:
            return
                PROCESS_FILE_RESULT_SUCCESS;

        case VFS_STAT_RESULT_INVALID_ARGUMENT:
            return
                PROCESS_FILE_RESULT_INVALID_ARGUMENT;

        case VFS_STAT_RESULT_NOT_SUPPORTED:
            return
                PROCESS_FILE_RESULT_NOT_SUPPORTED;
    }

    return
        PROCESS_FILE_RESULT_INVALID_ARGUMENT;
}

static enum process_file_result process_file_map_readiness_result(
    enum vfs_readiness_result result)
{
    switch (result) {
    case VFS_READINESS_RESULT_SUCCESS:
        return PROCESS_FILE_RESULT_SUCCESS;

    case VFS_READINESS_RESULT_INVALID_ARGUMENT:
        return PROCESS_FILE_RESULT_INVALID_ARGUMENT;

    case VFS_READINESS_RESULT_NOT_SUPPORTED:
        return PROCESS_FILE_RESULT_NOT_SUPPORTED;

    case VFS_READINESS_RESULT_ACCESS_DENIED:
        return PROCESS_FILE_RESULT_ACCESS_DENIED;
    }

    return PROCESS_FILE_RESULT_INVALID_ARGUMENT;
}

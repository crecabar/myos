// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static bool vfs_file_access_valid(
    enum vfs_open_access access
);

static bool vfs_file_state_valid(
    const struct vfs_file *file
);

static bool vfs_file_apply_signed_offset(
    uint64_t base,
    int64_t offset,
    uint64_t *result
);

static bool vfs_directory_entry_valid(
    const struct vfs_directory_entry *entry
);

static bool vfs_directory_entry_name_valid(
    const char *name,
    size_t name_length
);

// Public functions implementations
bool vfs_file_initialize(
    struct vfs_file *file,
    struct vfs_node *node,
    enum vfs_open_access access,
    const struct vfs_file_operations *operations,
    void *private_data)
{
    if (
        file == NULL ||
        node == NULL ||
        !vfs_file_access_valid(
            access
        )
    ) {
        return false;
    }

    if (!vfs_node_retain(
        node
    )) {
        return false;
    }

    file->reference_count =
        1;

    file->node =
        node;

    file->offset =
        0;

    file->access =
        access;

    file->operations =
        operations;

    file->private_data =
        private_data;

    return true;
}

bool vfs_file_retain(
    struct vfs_file *file)
{
    if (file == NULL) {
        return false;
    }

    if (
        file->reference_count == 0 ||
        file->reference_count == SIZE_MAX
    ) {
        return false;
    }

    ++file->reference_count;

    return true;
}

bool vfs_file_release(
    struct vfs_file *file)
{
    if (file == NULL) {
        return false;
    }

    if (file->reference_count == 0) {
        return false;
    }

    --file->reference_count;

    if (file->reference_count != 0) {
        return true;
    }

    struct vfs_node *node =
        file->node;

    const struct vfs_file_operations *operations =
        file->operations;

    if (
        operations != NULL &&
        operations->destroy != NULL
    ) {
        operations->destroy(
            file
        );
    }

    /*
     * destroy() may release the storage containing file. Only the retained
     * node pointer captured above remains valid to use here.
     */
    return vfs_node_release(
        node
    );
}

enum vfs_io_result vfs_file_read(
    struct vfs_file *file,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    if (
        bytes_read == NULL ||
        !vfs_file_state_valid(
            file
        )
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        (file->access &
            VFS_OPEN_ACCESS_READ) == 0
    ) {
        return
            VFS_IO_RESULT_ACCESS_DENIED;
    }

    /*
     * A zero-length read is successful once the file and access mode have
     * been validated. No filesystem callback or buffer is required.
     */
    if (size == 0) {
        *bytes_read =
            0;

        return
            VFS_IO_RESULT_SUCCESS;
    }

    if (buffer == NULL) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        file->operations == NULL ||
        file->operations->read == NULL
    ) {
        return
            VFS_IO_RESULT_NOT_SUPPORTED;
    }

    uint64_t original_offset =
        file->offset;

    size_t transferred =
        0;

    enum vfs_io_result io_result =
        file->operations->read(
            file,
            original_offset,
            buffer,
            size,
            &transferred
        );

    /*
     * The filesystem callback receives the generic offset but does not own it.
     * Restore it before rejecting any callback that mutated shared open-file
     * state directly.
     */
    if (file->offset != original_offset) {
        file->offset =
            original_offset;

        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    switch (io_result) {
        case VFS_IO_RESULT_SUCCESS:
            break;

        case VFS_IO_RESULT_INVALID_ARGUMENT:
        case VFS_IO_RESULT_NOT_SUPPORTED:
        case VFS_IO_RESULT_ACCESS_DENIED:
        case VFS_IO_RESULT_RESOURCE_EXHAUSTED:
            return
                io_result;
    }

    if (io_result != VFS_IO_RESULT_SUCCESS) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (transferred > size) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        (uint64_t) transferred >
        UINT64_MAX - original_offset
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    file->offset =
        original_offset +
        (uint64_t) transferred;

    *bytes_read =
        transferred;

    return
        VFS_IO_RESULT_SUCCESS;
}

enum vfs_io_result vfs_file_read_at(
    struct vfs_file *file,
    uint64_t offset,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    if (
        bytes_read == NULL ||
        !vfs_file_state_valid(
            file
        )
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        (file->access &
            VFS_OPEN_ACCESS_READ) == 0
    ) {
        return
            VFS_IO_RESULT_ACCESS_DENIED;
    }

    /*
     * Positioned byte I/O is meaningful only for regular files. Stream-like
     * objects such as character devices retain their ordinary read semantics.
     */
    if (
        file->node->type !=
            VFS_NODE_TYPE_REGULAR_FILE
    ) {
        return
            VFS_IO_RESULT_NOT_SUPPORTED;
    }

    /*
     * A zero-length positioned read succeeds once file state, access mode and
     * object type have been validated. No filesystem callback or buffer is
     * required.
     */
    if (size == 0) {
        *bytes_read =
            0;

        return
            VFS_IO_RESULT_SUCCESS;
    }

    if (buffer == NULL) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        file->operations == NULL ||
        file->operations->read == NULL
    ) {
        return
            VFS_IO_RESULT_NOT_SUPPORTED;
    }

    uint64_t original_offset =
        file->offset;

    size_t transferred =
        0;

    enum vfs_io_result io_result =
        file->operations->read(
            file,
            offset,
            buffer,
            size,
            &transferred
        );

    /*
     * Positioned reads never transfer ownership of the shared open-file
     * offset to the filesystem callback. Restore and reject any callback that
     * mutates it directly.
     */
    if (file->offset != original_offset) {
        file->offset =
            original_offset;

        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    switch (io_result) {
        case VFS_IO_RESULT_SUCCESS:
            break;

        case VFS_IO_RESULT_INVALID_ARGUMENT:
        case VFS_IO_RESULT_NOT_SUPPORTED:
        case VFS_IO_RESULT_ACCESS_DENIED:
        case VFS_IO_RESULT_RESOURCE_EXHAUSTED:
            return
                io_result;
    }

    if (io_result != VFS_IO_RESULT_SUCCESS) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (transferred > size) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Unlike ordinary read(), no offset arithmetic is performed here. The
     * caller-supplied position belongs only to this operation and the shared
     * open-file offset remains unchanged.
     */
    *bytes_read =
        transferred;

    return
        VFS_IO_RESULT_SUCCESS;
}

enum vfs_io_result vfs_file_write(
    struct vfs_file *file,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    if (
        bytes_written == NULL ||
        !vfs_file_state_valid(
            file
        )
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        (file->access &
            VFS_OPEN_ACCESS_WRITE) == 0
    ) {
        return
            VFS_IO_RESULT_ACCESS_DENIED;
    }

    /*
     * A zero-length write is successful once the file and access mode have
     * been validated. No filesystem callback or buffer is required.
     */
    if (size == 0) {
        *bytes_written =
            0;

        return
            VFS_IO_RESULT_SUCCESS;
    }

    if (buffer == NULL) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        file->operations == NULL ||
        file->operations->write == NULL
    ) {
        return
            VFS_IO_RESULT_NOT_SUPPORTED;
    }

    uint64_t original_offset =
        file->offset;

    size_t transferred =
        0;

    enum vfs_io_result io_result =
        file->operations->write(
            file,
            original_offset,
            buffer,
            size,
            &transferred
        );

    if (file->offset != original_offset) {
        file->offset =
            original_offset;

        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    switch (io_result) {
        case VFS_IO_RESULT_SUCCESS:
            break;

        case VFS_IO_RESULT_INVALID_ARGUMENT:
        case VFS_IO_RESULT_NOT_SUPPORTED:
        case VFS_IO_RESULT_ACCESS_DENIED:
        case VFS_IO_RESULT_RESOURCE_EXHAUSTED:
            return
                io_result;
    }

    if (io_result != VFS_IO_RESULT_SUCCESS) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (transferred > size) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        (uint64_t) transferred >
        UINT64_MAX - original_offset
    ) {
        return
            VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    file->offset =
        original_offset +
        (uint64_t) transferred;

    *bytes_written =
        transferred;

    return
        VFS_IO_RESULT_SUCCESS;
}

enum vfs_directory_read_result vfs_file_read_directory(
    struct vfs_file *file,
    struct vfs_directory_entry *result)
{
    if (
        result == NULL ||
        !vfs_file_state_valid(
            file
        )
    ) {
        return
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
    }

    if (
        file->node->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            VFS_DIRECTORY_READ_RESULT_NOT_DIRECTORY;
    }

    if (
        (file->access &
            VFS_OPEN_ACCESS_READ) == 0
    ) {
        return
            VFS_DIRECTORY_READ_RESULT_ACCESS_DENIED;
    }

    if (
        file->operations == NULL ||
        file->operations->read_directory == NULL
    ) {
        return
            VFS_DIRECTORY_READ_RESULT_NOT_SUPPORTED;
    }

    uint64_t original_offset =
        file->offset;

    struct vfs_directory_entry entry = {0};

    enum vfs_directory_read_result read_result =
        file->operations->read_directory(
            file,
            &entry
        );

    /*
     * Directory enumeration owns filesystem-specific cursor state, not the
     * generic byte offset. Reject callbacks that mutate shared VFS offset
     * state.
     */
    if (file->offset != original_offset) {
        file->offset =
            original_offset;

        return
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
    }

    switch (read_result) {
        case VFS_DIRECTORY_READ_RESULT_ENTRY:
            break;

        case VFS_DIRECTORY_READ_RESULT_END:
        case VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT:
        case VFS_DIRECTORY_READ_RESULT_NOT_DIRECTORY:
        case VFS_DIRECTORY_READ_RESULT_NOT_SUPPORTED:
        case VFS_DIRECTORY_READ_RESULT_ACCESS_DENIED:
        case VFS_DIRECTORY_READ_RESULT_INVALIDATED:
        case VFS_DIRECTORY_READ_RESULT_RESOURCE_EXHAUSTED:
            return
                read_result;
    }

    if (
        read_result !=
            VFS_DIRECTORY_READ_RESULT_ENTRY ||
        !vfs_directory_entry_valid(
            &entry
        )
    ) {
        return
            VFS_DIRECTORY_READ_RESULT_INVALID_ARGUMENT;
    }

    *result =
        entry;

    return
        VFS_DIRECTORY_READ_RESULT_ENTRY;
}

enum vfs_seek_result vfs_file_seek(
    struct vfs_file *file,
    int64_t offset,
    enum vfs_seek_origin origin,
    uint64_t *result)
{
    if (
        result == NULL ||
        !vfs_file_state_valid(
            file
        )
    ) {
        return
            VFS_SEEK_RESULT_INVALID_ARGUMENT;
    }

    if (
        file->node->type !=
        VFS_NODE_TYPE_REGULAR_FILE
    ) {
        return
            VFS_SEEK_RESULT_NOT_SUPPORTED;
    }

    uint64_t base;

    switch (origin) {
        case VFS_SEEK_ORIGIN_START:
            base =
                0;
            break;

        case VFS_SEEK_ORIGIN_CURRENT:
            base =
                file->offset;
            break;

        case VFS_SEEK_ORIGIN_END: {
            struct vfs_stat metadata;

            enum vfs_stat_result stat_result =
                vfs_node_stat(
                    file->node,
                    &metadata
                );

            switch (stat_result) {
                case VFS_STAT_RESULT_SUCCESS:
                    base =
                        metadata.size;
                    break;

                case VFS_STAT_RESULT_NOT_SUPPORTED:
                    return
                        VFS_SEEK_RESULT_NOT_SUPPORTED;

                case VFS_STAT_RESULT_INVALID_ARGUMENT:
                    return
                        VFS_SEEK_RESULT_INVALID_ARGUMENT;
            }

            if (
                stat_result !=
                VFS_STAT_RESULT_SUCCESS
            ) {
                return
                    VFS_SEEK_RESULT_INVALID_ARGUMENT;
            }

            break;
        }

        default:
            return
                VFS_SEEK_RESULT_INVALID_ARGUMENT;
    }

    uint64_t new_offset;

    if (!vfs_file_apply_signed_offset(
        base,
        offset,
        &new_offset
    )) {
        return
            VFS_SEEK_RESULT_INVALID_ARGUMENT;
    }

    file->offset =
        new_offset;

    *result =
        new_offset;

    return
        VFS_SEEK_RESULT_SUCCESS;
}

// Private functions and helpers implementations
static bool vfs_file_access_valid(
    enum vfs_open_access access)
{
    const unsigned int valid_access =
        VFS_OPEN_ACCESS_READ |
        VFS_OPEN_ACCESS_WRITE;

    return
        access != 0 &&
        ((unsigned int) access & ~valid_access) == 0;
}

static bool vfs_file_state_valid(
    const struct vfs_file *file)
{
    if (
        file == NULL ||
        file->reference_count == 0 ||
        file->node == NULL
    ) {
        return false;
    }

    if (file->node->reference_count == 0) {
        return false;
    }

    return vfs_file_access_valid(
        file->access
    );
}

static bool vfs_file_apply_signed_offset(
    uint64_t base,
    int64_t offset,
    uint64_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (offset >= 0) {
        uint64_t positive_offset =
            (uint64_t) offset;

        if (
            positive_offset >
            UINT64_MAX - base
        ) {
            return false;
        }

        *result =
            base +
            positive_offset;

        return true;
    }

    /*
     * Avoid negating INT64_MIN directly. offset + 1 is representable for every
     * negative int64_t value, so its magnitude can be completed in uint64_t.
     */
    uint64_t negative_magnitude =
        (uint64_t) (
            -(offset + 1)
        ) + 1;

    if (negative_magnitude > base) {
        return false;
    }

    *result =
        base -
        negative_magnitude;

    return true;
}

static bool vfs_directory_entry_valid(
    const struct vfs_directory_entry *entry)
{
    if (
        entry == NULL ||
        !vfs_directory_entry_name_valid(
            entry->name,
            entry->name_length
        )
    ) {
        return false;
    }

    switch (entry->type) {
        case VFS_NODE_TYPE_REGULAR_FILE:
        case VFS_NODE_TYPE_DIRECTORY:
        case VFS_NODE_TYPE_CHARACTER_DEVICE:
            return true;
    }

    return false;
}

static bool vfs_directory_entry_name_valid(
    const char *name,
    size_t name_length)
{
    if (
        name == NULL ||
        name_length == 0 ||
        name_length > VFS_NAME_MAX ||
        name[name_length] != '\0'
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        if (
            name[index] == '\0' ||
            name[index] == '/'
        ) {
            return false;
        }
    }

    if (
        name_length == 1 &&
        name[0] == '.'
    ) {
        return false;
    }

    if (
        name_length == 2 &&
        name[0] == '.' &&
        name[1] == '.'
    ) {
        return false;
    }

    return true;
}

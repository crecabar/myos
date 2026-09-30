// SPDX-License-Identifier: GPL-2.0-only

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

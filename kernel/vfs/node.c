// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static bool vfs_node_type_valid(
    enum vfs_node_type type
);

static bool vfs_open_access_valid(
    enum vfs_open_access access
);

static bool vfs_lookup_name_valid(
    const char *name,
    size_t name_length
);

// Public functions implementations
bool vfs_node_initialize(
    struct vfs_node *node,
    enum vfs_node_type type,
    const struct vfs_node_operations *operations,
    void *private_data)
{
    if (node == NULL) {
        return false;
    }

    if (!vfs_node_type_valid(type)) {
        return false;
    }

    node->type =
        type;

    node->reference_count =
        1;

    node->operations =
        operations;

    node->private_data =
        private_data;

    return true;
}

bool vfs_node_retain(
    struct vfs_node *node)
{
    if (node == NULL) {
        return false;
    }

    if (
        node->reference_count == 0 ||
        node->reference_count == SIZE_MAX
    ) {
        return false;
    }

    ++node->reference_count;

    return true;
}

bool vfs_node_release(
    struct vfs_node *node)
{
    if (node == NULL) {
        return false;
    }

    if (node->reference_count == 0) {
        return false;
    }

    --node->reference_count;

    if (node->reference_count != 0) {
        return true;
    }

    const struct vfs_node_operations *operations =
        node->operations;

    if (
        operations != NULL &&
        operations->destroy != NULL
    ) {
        operations->destroy(
            node
        );
    }

    /*
     * Do not inspect node after destroy(). The filesystem is allowed to have
     * released the storage containing it.
     */
    return true;
}

enum vfs_open_result vfs_node_open(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result)
{
    if (
        node == NULL ||
        result == NULL ||
        !vfs_open_access_valid(
            access
        )
    ) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (node->reference_count == 0) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (
        node->operations == NULL ||
        node->operations->open == NULL
    ) {
        return
            VFS_OPEN_RESULT_NOT_SUPPORTED;
    }

    struct vfs_file *file =
        NULL;

    enum vfs_open_result open_result =
        node->operations->open(
            node,
            access,
            &file
        );

    if (
        open_result !=
        VFS_OPEN_RESULT_OPENED
    ) {
        switch (open_result) {
            case VFS_OPEN_RESULT_INVALID_ARGUMENT:
            case VFS_OPEN_RESULT_NOT_SUPPORTED:
            case VFS_OPEN_RESULT_ACCESS_DENIED:
            case VFS_OPEN_RESULT_RESOURCE_EXHAUSTED:
                return
                    open_result;

            case VFS_OPEN_RESULT_OPENED:
                break;
        }

        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (
        file == NULL ||
        file->reference_count == 0
    ) {
        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    if (
        file->node != node ||
        file->access != access
    ) {
        /*
         * OPENED transferred one owned file reference. Drop that ownership before
         * rejecting filesystem state that violates the open contract.
         */
        (void) vfs_file_release(
            file
        );

        return
            VFS_OPEN_RESULT_INVALID_ARGUMENT;
    }

    *result =
        file;

    return
        VFS_OPEN_RESULT_OPENED;
}

enum vfs_stat_result vfs_node_stat(
    struct vfs_node *node,
    struct vfs_stat *result)
{
    if (
        node == NULL ||
        result == NULL
    ) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    if (node->reference_count == 0) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    if (
        node->operations == NULL ||
        node->operations->stat == NULL
    ) {
        return
            VFS_STAT_RESULT_NOT_SUPPORTED;
    }

    struct vfs_stat metadata = {
        .type =
            node->type,
        .size =
            0,
    };

    enum vfs_stat_result stat_result =
        node->operations->stat(
            node,
            &metadata
        );

    if (
        stat_result !=
        VFS_STAT_RESULT_SUCCESS
    ) {
        switch (stat_result) {
            case VFS_STAT_RESULT_INVALID_ARGUMENT:
            case VFS_STAT_RESULT_NOT_SUPPORTED:
                return
                    stat_result;

            case VFS_STAT_RESULT_SUCCESS:
                break;
        }

        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    if (metadata.type != node->type) {
        return
            VFS_STAT_RESULT_INVALID_ARGUMENT;
    }

    *result =
        metadata;

    return
        VFS_STAT_RESULT_SUCCESS;
}

enum vfs_lookup_result vfs_node_lookup(
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
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    if (directory->reference_count == 0) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    if (!vfs_lookup_name_valid(
        name,
        name_length
    )) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    if (
        directory->type !=
        VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            VFS_LOOKUP_RESULT_NOT_DIRECTORY;
    }

    if (
        directory->operations == NULL ||
        directory->operations->lookup == NULL
    ) {
        return
            VFS_LOOKUP_RESULT_NOT_SUPPORTED;
    }

    struct vfs_node *child =
        NULL;

    enum vfs_lookup_result lookup_result =
        directory->operations->lookup(
            directory,
            name,
            name_length,
            &child
        );

    if (
        lookup_result !=
        VFS_LOOKUP_RESULT_FOUND
    ) {
        /*
         * Filesystem callbacks must leave their result unchanged on failure.
         * child began NULL, so a non-NULL value here violates that contract.
         */
        if (child != NULL) {
            return
                VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
        }

        switch (lookup_result) {
            case VFS_LOOKUP_RESULT_NOT_FOUND:
            case VFS_LOOKUP_RESULT_INVALID_ARGUMENT:
            case VFS_LOOKUP_RESULT_NOT_DIRECTORY:
            case VFS_LOOKUP_RESULT_NOT_SUPPORTED:
            case VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED:
                return
                    lookup_result;

            case VFS_LOOKUP_RESULT_FOUND:
                break;
        }

        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    if (child == NULL) {
        return
            VFS_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Filesystem lookup returns a borrowed child. The generic VFS turns that
     * into the owned reference exposed to its caller.
     */
    if (!vfs_node_retain(
        child
    )) {
        return
            VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED;
    }

    *result =
        child;

    return
        VFS_LOOKUP_RESULT_FOUND;
}

enum vfs_parent_result vfs_node_parent(
    struct vfs_node *directory,
    struct vfs_node **result)
{
    if (
        directory == NULL ||
        result == NULL
    ) {
        return
            VFS_PARENT_RESULT_INVALID_ARGUMENT;
    }

    if (directory->reference_count == 0) {
        return
            VFS_PARENT_RESULT_INVALID_ARGUMENT;
    }

    if (
        directory->type !=
        VFS_NODE_TYPE_DIRECTORY
    ) {
        return
            VFS_PARENT_RESULT_NOT_DIRECTORY;
    }

    if (
        directory->operations == NULL ||
        directory->operations->parent == NULL
    ) {
        return
            VFS_PARENT_RESULT_NOT_SUPPORTED;
    }

    struct vfs_node *parent =
        directory->operations->parent(
            directory
        );

    if (parent == NULL) {
        return
            VFS_PARENT_RESULT_NO_PARENT;
    }

    /*
     * Filesystem parent traversal returns a borrowed node. The generic VFS
     * turns that into the owned reference exposed to its caller.
     */
    if (!vfs_node_retain(
        parent
    )) {
        return
            VFS_PARENT_RESULT_RESOURCE_EXHAUSTED;
    }

    *result =
        parent;

    return
        VFS_PARENT_RESULT_FOUND;
}

// Private functions and helpers implementations
static bool vfs_node_type_valid(
    enum vfs_node_type type)
{
    switch (type) {
        case VFS_NODE_TYPE_REGULAR_FILE:
        case VFS_NODE_TYPE_DIRECTORY:
        case VFS_NODE_TYPE_CHARACTER_DEVICE:
            return true;
    }

    return false;
}

static bool vfs_open_access_valid(
    enum vfs_open_access access)
{
    const unsigned int valid_access =
        VFS_OPEN_ACCESS_READ |
        VFS_OPEN_ACCESS_WRITE;

    return
        access != 0 &&
        ((unsigned int) access & ~valid_access) == 0;
}

static bool vfs_lookup_name_valid(
    const char *name,
    size_t name_length)
{
    if (
        name == NULL ||
        name_length == 0 ||
        name_length > VFS_NAME_MAX
    ) {
        return false;
    }

    for (size_t index = 0;
         index < name_length;
         ++index) {
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

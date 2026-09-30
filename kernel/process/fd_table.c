// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "fd_table.h"

#include <stdbool.h>
#include <stddef.h>

// Private functions and helpers declarations
static bool process_fd_table_is_empty(
    const struct process_fd_table *table
);

// Public functions implementations
bool process_fd_table_initialize(
    struct process_fd_table *table)
{
    if (table == NULL) {
        return false;
    }

    for (size_t descriptor = 0;
         descriptor < PROCESS_FD_TABLE_CAPACITY;
         ++descriptor) {
        table->entries[descriptor] =
            NULL;
    }

    return true;
}

bool process_fd_table_install(
    struct process_fd_table *table,
    struct vfs_file *file,
    size_t *descriptor)
{
    if (
        table == NULL ||
        file == NULL ||
        descriptor == NULL
    ) {
        return false;
    }

    for (size_t candidate = 0;
         candidate < PROCESS_FD_TABLE_CAPACITY;
         ++candidate) {
        if (table->entries[candidate] != NULL) {
            continue;
        }

        if (!vfs_file_retain(
            file
        )) {
            return false;
        }

        table->entries[candidate] =
            file;

        *descriptor =
            candidate;

        return true;
    }

    return false;
}

struct vfs_file *process_fd_table_get(
    const struct process_fd_table *table,
    size_t descriptor)
{
    if (table == NULL) {
        return NULL;
    }

    if (
        descriptor >=
        PROCESS_FD_TABLE_CAPACITY
    ) {
        return NULL;
    }

    return
        table->entries[descriptor];
}

bool process_fd_table_close(
    struct process_fd_table *table,
    size_t descriptor)
{
    if (table == NULL) {
        return false;
    }

    if (
        descriptor >=
        PROCESS_FD_TABLE_CAPACITY
    ) {
        return false;
    }

    struct vfs_file *file =
        table->entries[descriptor];

    if (file == NULL) {
        return false;
    }

    /*
     * Remove the observable table reference before invoking release(), whose
     * final-reference path may destroy the file storage.
     */
    table->entries[descriptor] =
        NULL;

    return vfs_file_release(
        file
    );
}

bool process_fd_table_clone(
    struct process_fd_table *destination,
    const struct process_fd_table *source)
{
    if (
        destination == NULL ||
        source == NULL ||
        destination == source
    ) {
        return false;
    }

    if (!process_fd_table_is_empty(
        destination
    )) {
        return false;
    }

    for (size_t descriptor = 0;
         descriptor < PROCESS_FD_TABLE_CAPACITY;
         ++descriptor) {
        struct vfs_file *file =
            source->entries[descriptor];

        if (file == NULL) {
            continue;
        }

        if (!vfs_file_retain(
            file
        )) {
            if (!process_fd_table_release_all(
                destination
            )) {
                return false;
            }

            return false;
        }

        destination->entries[descriptor] =
            file;
    }

    return true;
}

bool process_fd_table_release_all(
    struct process_fd_table *table)
{
    if (table == NULL) {
        return false;
    }

    bool success =
        true;

    for (size_t descriptor = 0;
         descriptor < PROCESS_FD_TABLE_CAPACITY;
         ++descriptor) {
        struct vfs_file *file =
            table->entries[descriptor];

        if (file == NULL) {
            continue;
        }

        table->entries[descriptor] =
            NULL;

        if (!vfs_file_release(
            file
        )) {
            success =
                false;
        }
    }

    return success;
}

// Private functions and helpers implementations
static bool process_fd_table_is_empty(
    const struct process_fd_table *table)
{
    if (table == NULL) {
        return false;
    }

    for (size_t descriptor = 0;
         descriptor < PROCESS_FD_TABLE_CAPACITY;
         ++descriptor) {
        if (table->entries[descriptor] != NULL) {
            return false;
        }
    }

    return true;
}

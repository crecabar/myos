// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file initramfs_format.h
 * @brief Bounded parser for the initramfs CPIO newc format.
 *
 * The parser exposes archive entries to the initramfs filesystem
 * implementation without allocating memory or creating VFS objects.
 *
 * All returned pathname and data pointers borrow storage from the archive.
 * Therefore the archive byte range must remain alive while parsed entries are
 * being consumed.
 */

#ifndef MYOS_FS_INITRAMFS_FORMAT_H
#define MYOS_FS_INITRAMFS_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum initramfs_format_entry_type {
    INITRAMFS_FORMAT_ENTRY_REGULAR_FILE,
    INITRAMFS_FORMAT_ENTRY_DIRECTORY,
};

enum initramfs_format_result {
    INITRAMFS_FORMAT_RESULT_ENTRY,
    INITRAMFS_FORMAT_RESULT_END,
    INITRAMFS_FORMAT_RESULT_INVALID_ARGUMENT,
    INITRAMFS_FORMAT_RESULT_MALFORMED,
    INITRAMFS_FORMAT_RESULT_UNSUPPORTED_TYPE,
};

/**
 * One validated archive entry.
 *
 * path is a borrowed byte sequence whose length excludes the trailing NUL
 * stored in the archive.
 *
 * data borrows the corresponding archive payload. It is NULL when size is
 * zero.
 */
struct initramfs_format_entry {
    enum initramfs_format_entry_type type;

    const char *path;
    size_t path_length;

    const uint8_t *data;
    size_t size;
};

/**
 * Iteration state for one CPIO newc archive.
 *
 * Callers should treat these fields as parser-owned state after successful
 * initialization.
 */
struct initramfs_format_iterator {
    const uint8_t *archive;
    size_t archive_size;
    size_t offset;

    bool finished;
    bool failed;
};

/**
 * Initializes an iterator over one initramfs archive.
 *
 * Archive contents are borrowed and are not copied.
 *
 * A structurally malformed archive may still initialize successfully; format
 * validation occurs incrementally in initramfs_format_next().
 *
 * @param archive Archive bytes.
 * @param archive_size Number of archive bytes.
 * @param iterator Iterator storage to initialize.
 *
 * @return true when arguments are valid; false otherwise.
 */
bool initramfs_format_iterator_initialize(
    const void *archive,
    size_t archive_size,
    struct initramfs_format_iterator *iterator
);

/**
 * Parses the next validated archive entry.
 *
 * Successful ordinary entries return INITRAMFS_FORMAT_RESULT_ENTRY and publish
 * entry. Encountering TRAILER!!! returns INITRAMFS_FORMAT_RESULT_END.
 *
 * Malformed or unsupported archives poison the iterator. After the first
 * archive failure, subsequent attempts return
 * INITRAMFS_FORMAT_RESULT_MALFORMED rather than resuming from partially
 * validated state.
 *
 * entry is modified only when INITRAMFS_FORMAT_RESULT_ENTRY is returned.
 *
 * @param iterator Initialized iterator.
 * @param entry Receives one validated ordinary entry.
 *
 * @return Detailed parser result.
 */
enum initramfs_format_result initramfs_format_next(
    struct initramfs_format_iterator *iterator,
    struct initramfs_format_entry *entry
);

#endif

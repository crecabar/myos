// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file user_copy.h
 * @brief Validated kernel/userspace memory copy boundary.
 *
 * Kernel entry points that consume or produce data through untrusted
 * userspace pointers must use this boundary rather than process_memory_read()
 * or process_memory_write() directly.
 *
 * Non-empty copies validate the complete userspace range before moving any
 * data. Zero-length copies succeed without validating or dereferencing either
 * pointer.
 */

#ifndef MYOS_PROCESS_USER_COPY_H
#define MYOS_PROCESS_USER_COPY_H

#include "memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Copies bytes from untrusted userspace into kernel-owned memory.
 *
 * The complete userspace range must:
 *
 * - belong to canonical process userspace;
 * - be backed exclusively by normal 4 KiB mappings;
 * - be accessible from CPL3 through every paging level.
 *
 * Validation is performed for the complete range before any destination
 * byte is modified. A failed operation therefore never exposes a partial
 * copy to the caller.
 *
 * A zero-length operation succeeds without dereferencing either address.
 *
 * @param memory Process address space containing the source range.
 * @param user_address First userspace byte to read.
 * @param destination Kernel-owned destination buffer.
 * @param size Number of bytes to copy.
 *
 * @return true when the complete copy succeeded; false otherwise.
 */
bool copy_from_user(
    const struct process_memory *memory,
    uint64_t user_address,
    void *destination,
    size_t size
);

/**
 * Copies bytes from kernel-owned memory into untrusted userspace.
 *
 * In addition to the copy_from_user() accessibility requirements, every
 * paging level covering the destination must permit writes from CPL3.
 *
 * Validation is performed for the complete range before any userspace byte
 * is modified. A failed operation therefore never performs a partial write.
 *
 * A zero-length operation succeeds without dereferencing either address.
 *
 * @param memory Process address space containing the destination range.
 * @param user_address First userspace byte to write.
 * @param source Kernel-owned source buffer.
 * @param size Number of bytes to copy.
 *
 * @return true when the complete copy succeeded; false otherwise.
 */
bool copy_to_user(
    struct process_memory *memory,
    uint64_t user_address,
    const void *source,
    size_t size
);

#endif

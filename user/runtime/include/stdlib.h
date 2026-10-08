// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file stdlib.h
 * @brief Minimal bootstrap C allocation interfaces.
 */

#ifndef MYOS_USER_RUNTIME_STDLIB_H
#define MYOS_USER_RUNTIME_STDLIB_H

#include <stddef.h>

/**
 * Allocates at least size bytes of suitably aligned userspace storage.
 *
 * The bootstrap allocator uses a process-private fixed arena contained in the
 * executable's writable BSS. Allocated storage remains valid until released
 * with free() or until the process image is replaced or destroyed.
 *
 * A zero-size request returns NULL in the bootstrap runtime.
 *
 * @param size Number of bytes requested.
 *
 * @return Pointer to suitably aligned storage, or NULL when the request cannot
 *         be satisfied.
 */
void *malloc(size_t size);

/**
 * Releases storage previously returned by malloc().
 *
 * Passing NULL has no effect.
 *
 * @param pointer Allocation to release, or NULL.
 */
void free(void *pointer);

#endif

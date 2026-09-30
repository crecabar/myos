// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file memory.h
 * @brief Freestanding compiler memory primitives.
 */

#ifndef MYOS_RUNTIME_MEMORY_H
#define MYOS_RUNTIME_MEMORY_H

#include <stddef.h>

/**
 * Copies size bytes from source to destination.
 *
 * The memory ranges must not overlap.
 *
 * @param destination Destination memory.
 * @param source Source memory.
 * @param size Number of bytes to copy.
 *
 * @return destination.
 */
void *memcpy(
    void *restrict destination,
    const void *restrict source,
    size_t size
);

/**
 * Fills size bytes of memory with the low eight bits of value.
 *
 * @param destination Destination memory.
 * @param value Byte value, interpreted as unsigned char.
 * @param size Number of bytes to fill.
 *
 * @return destination.
 */
void *memset(
    void *destination,
    int value,
    size_t size
);

#endif

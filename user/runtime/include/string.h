// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file string.h
 * @brief Minimal bootstrap C memory and string interfaces.
 */

#ifndef MYOS_USER_RUNTIME_STRING_H
#define MYOS_USER_RUNTIME_STRING_H

#include <stddef.h>

/**
 * Copies size bytes from source to destination.
 *
 * The source and destination ranges must not overlap.
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
 * Copies size bytes between potentially overlapping memory ranges.
 *
 * @param destination Destination memory.
 * @param source Source memory.
 * @param size Number of bytes to copy.
 *
 * @return destination.
 */
void *memmove(
    void *destination,
    const void *source,
    size_t size
);

/**
 * Fills size bytes with the low eight bits of value.
 *
 * @param destination Destination memory.
 * @param value Byte value.
 * @param size Number of bytes to fill.
 *
 * @return destination.
 */
void *memset(
    void *destination,
    int value,
    size_t size
);

/**
 * Compares size bytes from two memory regions.
 *
 * @param left First memory region.
 * @param right Second memory region.
 * @param size Number of bytes to compare.
 *
 * @return Zero when equal, a negative value when left sorts before right,
 * or a positive value when left sorts after right.
 */
int memcmp(
    const void *left,
    const void *right,
    size_t size
);

/**
 * Returns the number of characters before the terminating null byte.
 *
 * @param string Null-terminated string.
 *
 * @return String length excluding the terminating null byte.
 */
size_t strlen(const char *string);

/**
 * Compares two null-terminated strings.
 *
 * @param left First string.
 * @param right Second string.
 *
 * @return Zero when equal, a negative value when left sorts before right,
 * or a positive value when left sorts after right.
 */
int strcmp(
    const char *left,
    const char *right
);

/**
 * Compares at most size characters from two strings.
 *
 * @param left First string.
 * @param right Second string.
 * @param size Maximum number of characters to compare.
 *
 * @return Zero when equal over the requested range, a negative value when
 * left sorts before right, or a positive value when left sorts after right.
 */
int strncmp(
    const char *left,
    const char *right,
    size_t size
);

#endif

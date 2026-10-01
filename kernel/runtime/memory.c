// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file memory.c
 * @brief Freestanding compiler memory primitives.
 */

#include "memory.h"

#include <stddef.h>
#include <stdint.h>

// Public functions implementations
void *memcpy(
    void *restrict destination,
    const void *restrict source,
    size_t size)
{
    uint8_t *destination_bytes =
        destination;

    const uint8_t *source_bytes =
        source;

    for (
        size_t index = 0;
        index < size;
        ++index
    ) {
        destination_bytes[index] =
            source_bytes[index];
    }

    return destination;
}

void *memset(
    void *destination,
    int value,
    size_t size)
{
    uint8_t *destination_bytes =
        destination;

    uint8_t byte =
        (uint8_t) value;

    for (
        size_t index = 0;
        index < size;
        ++index
    ) {
        destination_bytes[index] =
            byte;
    }

    return destination;
}

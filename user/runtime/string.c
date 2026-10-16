// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file string.c
 * @brief Minimal bootstrap C memory and string primitives.
 */

#include <string.h>

#include <stddef.h>
#include <stdint.h>

void *memcpy(
    void *restrict destination,
    const void *restrict source,
    size_t size)
{
    unsigned char *destination_bytes =
        destination;

    const unsigned char *source_bytes =
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

void *memmove(
    void *destination,
    const void *source,
    size_t size)
{
    unsigned char *destination_bytes =
        destination;

    const unsigned char *source_bytes =
        source;

    uintptr_t destination_address =
        (uintptr_t) destination;

    uintptr_t source_address =
        (uintptr_t) source;

    if (
        destination_address ==
            source_address ||
        size == 0
    ) {
        return destination;
    }

    if (
        destination_address <
        source_address
    ) {
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

    for (
        size_t index = size;
        index > 0;
        --index
    ) {
        destination_bytes[index - 1] =
            source_bytes[index - 1];
    }

    return destination;
}

void *memset(
    void *destination,
    int value,
    size_t size)
{
    unsigned char *destination_bytes =
        destination;

    unsigned char byte =
        (unsigned char) value;

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

int memcmp(
    const void *left,
    const void *right,
    size_t size)
{
    const unsigned char *left_bytes =
        left;

    const unsigned char *right_bytes =
        right;

    for (
        size_t index = 0;
        index < size;
        ++index
    ) {
        if (
            left_bytes[index] !=
            right_bytes[index]
        ) {
            return
                (int) left_bytes[index] -
                (int) right_bytes[index];
        }
    }

    return 0;
}

size_t strlen(const char *string)
{
    size_t length = 0;

    while (string[length] != '\0') {
        ++length;
    }

    return length;
}

int strcmp(
    const char *left,
    const char *right)
{
    size_t index = 0;

    while (
        left[index] != '\0' &&
        left[index] == right[index]
    ) {
        ++index;
    }

    return
        (int) (unsigned char) left[index] -
        (int) (unsigned char) right[index];
}

int strncmp(
    const char *left,
    const char *right,
    size_t size)
{
    for (
        size_t index = 0;
        index < size;
        ++index
    ) {
        unsigned char left_byte =
            (unsigned char) left[index];

        unsigned char right_byte =
            (unsigned char) right[index];

        if (
            left_byte != right_byte ||
            left_byte == '\0'
        ) {
            return
                (int) left_byte -
                (int) right_byte;
        }
    }

    return 0;
}
